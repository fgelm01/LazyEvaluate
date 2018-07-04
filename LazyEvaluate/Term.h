#ifndef OQTON_LAZY_EVALUATE_TERM_H
#define OQTON_LAZY_EVALUATE_TERM_H
#include "lang_utils/tuple.h"
#include <type_traits>

namespace libraries_oqton {
namespace LazyEvaluate {

class TermBase {
protected:
  virtual void doEvaluate() = 0;

  std::vector<TermBase*> m_children;
  std::vector<TermBase*> m_parents;
}

template <typename RESULT>
class Term : public TermBase {
private:
  using state_t = enum { UNSTARTED, PENDING, EVALUATED };
  using starter_t = std::function<std::future<RESULT>(std::mutex&, std::condition_variable&, TermBase&*)>;
  
  template <typename ELEMENT>
  auto get_element_product(ELEMENT &&element) {
    if constexpr (
      std::is_base_of<TermBase, typename std::decay<ELEMENT>::type>>::value) {
        return *element;
      }
    else {
      return element;
    }
  }

  
  
public:
  Term() : m_state(UNSTARTED) {}
  void set_starter(starter_t &starter) {
    m_starter = starter;
  }
  Term(starter_t &starter) : m_starter(starter) { }
    
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
        if (seen.find(cur) == seen.end())
          continue;

        seen.emplace(cur);
        if (cur->m_children.empty())
          undependent.push_back(cur);

        for (TermBase* child : cur->m_children)
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
        cv.wait(lk, [&done](){ return done != NULL });
        done_local = done;
        done = NULL;
      }
      **done_local; //force state change, should not block
      for (TermBase *parent : done_local->m_parents) {
        parent->doEvaluate(m, cv, done);
      }
    }
  }

  void doEvaluate(std::mutex &m, std::condition_variable &cv, TermBase &*done) {
    for (auto child : m_children) {
      //m_state is atomic, and a state will never transition back from evaluated
      //so it is safe to check if children are evaluated without locking
      if (child->m_state != EVALUATED)
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state == UNSTARTED) {
      m_future = m_calculation(m, cv, done, m_children);
      m_state = PENDING;
    }
  }
  
  const RESULT& operator*() {
    //non-locked check of atomic to see if we can skip right to return
    if (m_state != EVALUATED) {
      //we want to be locked when we're starting evaluation, and when we're
      //retrieving results, but not while we're waiting
      evaluate();
      m_future.wait();
      
      std::lock_guard<std::mutex> lock(m_mutex);
      //double check for race conditions
      if (m_state != EVALUATED)
        m_value = m_future.get();
    }

    //if we get here we have guaranteed that m_value has data
    return m_value;
  }
           
private:
  std::future<RESULT> m_future;
  CALCULATION m_calculation;

  std::atomic<state_t> m_state;
  std::mutex m_mutex;
  RESULT m_value;
};

}
}

#endif
