#ifndef OQTON_LAZY_EVALUATE_TERM_H
#define OQTON_LAZY_EVALUATE_TERM_H
#include "lang_utils/tuple.h"
#include <type_traits>
#include <future>
#include <set>

namespace libraries_oqton {
namespace LazyEvaluate {

class TermBase {
public:
  using state_t = enum { UNSTARTED, PENDING, EVALUATED };
  TermBase() : m_state(UNSTARTED) {}
  TermBase(const state_t state) : m_state(state) {}
  virtual void doEvaluate(std::mutex &m, std::condition_variable &cv,
                          TermBase *&done) = 0;
  virtual void finalize() = 0;

  std::vector<TermBase*> m_children;
  std::vector<TermBase*> m_parents;
  std::atomic<state_t> m_state;
};

template <typename RESULT>
class CalculationBase {
public:
  virtual std::future<RESULT> operator()(std::mutex&,
                                         std::condition_variable&,
                                         TermBase*,
                                         TermBase*&,
                                         const std::vector<TermBase*>&) = 0;
};

template <typename RESULT>
class Term : public TermBase {
public:
  Term() : TermBase() {}

  Term(const RESULT &value) : TermBase(EVALUATED), m_value(value) {}
  //Term(RESULT &&value) : TermBase(EVALUATED), m_value(std::move(value)) {}

  template <typename CALCULATION>
  void set_calculation(CALCULATION &calculation) {
    m_calculation = &calculation;
  }
  template <typename CALCULATION>
  void set_calculation(CALCULATION &&calculation) {
    m_calculation = new CALCULATION(std::move(calculation));
  }
  template <typename ...TERMS>
  void set_terms(TERMS& ...terms) {
    m_children = {(&terms)...};
    for (auto child : m_children) {
      child->m_parents.push_back(this);
    }      
  }
  
  //Term(starter_t &calculation) : m_calculation(calculation) { }
    
  //begins evaluation, returns immediately, is safe to call multiple times
  void evaluate() {
    //build initial set of undepended terms
    std::vector<TermBase*> undependent;
    std::set<TermBase*> seen;
    std::vector<TermBase*> working;
    
    working.push_back(this);
    while(!working.empty()) {
      std::vector<TermBase*> next;
      for (TermBase* cur : working) {
        if (seen.find(cur) != seen.end())
          continue;

        seen.emplace(cur);
        bool dependents = false;
        for (auto &child : cur->m_children)
          if (child->m_state != EVALUATED) {
            dependents = true;
            break;
          }
        
        if (!dependents)
          undependent.push_back(cur);

        for (TermBase* child : cur->m_children)
          if (child->m_state != EVALUATED)
            next.push_back(child);
      }
      working = std::move(next);
    }

    std::mutex m;
    std::condition_variable cv;
    TermBase *done = NULL;
    TermBase *done_local = NULL;
    //evaluate each undepended term, passing done condition, get evaluating list
    for (TermBase *next : undependent)
      next->doEvaluate(m, cv, done);

    while (m_state != EVALUATED) {
      {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&done](){ return done != NULL; });
        done_local = done;
        done = NULL;
      }
      done_local->finalize(); //force state change, should not block
      for (TermBase *parent : done_local->m_parents) {
        parent->doEvaluate(m, cv, done);
      }
    }
  }

  virtual void doEvaluate(std::mutex &m, std::condition_variable &cv, TermBase *&done) {
    for (auto child : m_children) {
      //m_state is atomic, and a state will never transition back from evaluated
      //so it is safe to check if children are evaluated without locking
      if (child->m_state != EVALUATED)
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state == UNSTARTED) {
      m_future = (*m_calculation)(m, cv, this, done, m_children);
      m_state = PENDING;
    }
  }
  
  const RESULT& operator*() {
    if (m_state == UNSTARTED)
      evaluate();
    
    finalize();
    //if we get here we have guaranteed that m_value has data
    return m_value;
  }
    

  virtual void finalize() {
    //non-locked check of atomic to see if we can skip right to return
    if (m_state == PENDING) {
      //we want to be locked when we're starting evaluation, and when we're
      //retrieving results, but not while we're waiting
      m_future.wait();

      std::lock_guard<std::mutex> lock(m_mutex);
      //double check for race conditions
      if (m_state != EVALUATED) {
        m_value = m_future.get();
        m_state = EVALUATED;
      }
    }
  }
           
private:
  std::future<RESULT> m_future;
  CalculationBase<RESULT> *m_calculation;

  std::mutex m_mutex;
  RESULT m_value;
};

}
}

#endif
