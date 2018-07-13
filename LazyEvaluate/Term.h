#ifndef OQTON_LAZY_EVALUATE_TERM_H
#define OQTON_LAZY_EVALUATE_TERM_H
#include "ThreadPoolSingleton.h"
#include "lang_utils/tuple.h"
#include <type_traits>
#include <experimental/type_traits>
#include <future>
#include <set>

#include <iostream>

namespace libraries_oqton {
namespace LazyEvaluate {

template<typename T>
using clear_method_t =
  decltype( std::declval<T&>().clear() );

template <typename VALUE>
bool clear_data(VALUE &&value) {
  if constexpr (std::experimental::is_detected_v<clear_method_t, VALUE>) {
    value.clear();
    return true;
  }
  else {
    return false;
  }
}

class TermBase {
public:
  using state_t = enum { UNSTARTED, PENDING, EVALUATED };
  TermBase() : m_state(UNSTARTED) {}
  TermBase(const state_t state) : m_state(state) {}

  TermBase(TermBase &&other)
    : m_children(std::move(other.m_children)),
      m_allocated(std::move(other.m_allocated)),
      m_parents(std::move(other.m_parents)),
      m_state(other.m_state.load()) {
    other.m_state.store(UNSTARTED);
  }

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
      next->apply(m, cv, done);

    while (m_state != EVALUATED) {
      {
        std::unique_lock<std::mutex> lk(m);
        while (done == NULL) {
          cv.wait(lk);
          if (done == NULL) {
            lk.unlock();	
            cv.notify_one();
            lk.lock();
          }
        }
        done_local = done;
        done = NULL;
        lk.unlock();
      }
      cv.notify_one();
      done_local->finalize(); //force state change, should not block
      for (TermBase *parent : done_local->m_parents) {
        if (parent->children_evaluated()) {
          parent->apply(m, cv, done);
        }
      }
      for (TermBase *child : done_local->m_children) {
        if (child->parents_evaluated()) {
          child->clear();
        }
      }
    }
  }

  bool children_evaluated() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto &child : m_children)
      if (child->m_state != EVALUATED)
        return false;

    return true;
  }

  bool parents_evaluated() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto &parent : m_parents)
      if (parent->m_state != EVALUATED)
        return false;

    return true;
  }

  virtual void apply(std::mutex &m, std::condition_variable &cv,
                     TermBase *&done) = 0;
  virtual void finalize() = 0;
  virtual void clear() = 0;

  virtual ~TermBase() {
    assert(m_allocated.size() == m_children.size());
    for (size_t i = 0; i < m_allocated.size(); ++i) {
      if (m_allocated[i])
        delete m_children[i];
    }
  }
  
  template <typename TERM>
  void add_child(TERM &&child) {
    bool isrvalue = std::is_lvalue_reference<TERM>::value;
    if constexpr (std::is_lvalue_reference<TERM>::value) {
      m_children.push_back(&child);
      m_allocated.push_back(false);
    } else {
      m_children.push_back(new TERM(std::move(child)));
      m_allocated.push_back(true);
    }
    m_children.back()->m_parents.push_back(this);
  }
  
  std::vector<TermBase*> m_children;
  std::vector<bool> m_allocated;
  std::vector<TermBase*> m_parents;
  std::atomic<state_t> m_state;
  std::mutex m_mutex;
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
class TermValue : public TermBase {
public:
  using value_type = RESULT;

  TermValue(TermValue<RESULT> &&other)
    : m_future(std::move(other.m_future)),
      m_value(std::move(other.m_value)),
      TermBase(std::move(other)) {}
  
  TermValue(RESULT value) : TermBase(EVALUATED), m_value(value) {}
  //Term(RESULT &&value) : TermBase(EVALUATED), m_value(std::move(value)) {}

  const RESULT& no_eval_access() {
    return m_value;
  }
  
  const RESULT& operator*() {
    if (m_state == UNSTARTED)
      evaluate();
    
    finalize();
    //if we get here we have guaranteed that m_value has data
    return m_value;
  }

  const RESULT* operator->() {
    return &**this;
  }

  //stub implementation so we can declare typed terms
  virtual void apply(std::mutex &m, std::condition_variable &cv,
                     TermBase *&done) {}

  virtual void finalize() {
    //non-locked check of atomic to see if we can skip right to return
    if (m_state == PENDING) {
      std::lock_guard<std::mutex> lock(m_mutex);
      //we want to be locked when we're starting evaluation, and when we're
      //retrieving results, but not while we're waiting
      m_future.wait();

      //double check for race conditions
      if (m_state != EVALUATED) {
        m_value = m_future.get();
        m_state = EVALUATED;
      }
    }
  }

  virtual void clear() { }
    
  virtual ~TermValue() {}
  
protected:
  std::future<value_type> m_future;
  value_type m_value;
};

template <typename CALCULATION>
class Term : public TermValue<typename CALCULATION::return_t> {
public:
  using Typed = TermValue<typename CALCULATION::return_t>;
  using calculation_type = CALCULATION;

  Term(Term<CALCULATION> &&other)
    : m_calculation(std::move(other.m_calculation)),
      Typed(std::move(other)) {}
  
  template <typename ...ARGS>
  Term(ARGS&& ...args)
    : m_calculation(calculation_type(std::forward<ARGS>(args)...)),
      Typed(typename Typed::value_type()) {
    TermBase::m_state = TermBase::UNSTARTED;
  }
  
  template <typename ...TERMS>
  void terms(TERMS&& ...terms) {
    assert(TermBase::m_children.empty());
    m_calculation.check_terms(terms...);
    (TermBase::add_child(std::forward<TERMS>(terms)), ...);
  }
  
  //Term(starter_t &calculation) : m_calculation(calculation) { }
    
  virtual void apply(std::mutex &m, std::condition_variable &cv, TermBase *&done) {
#ifndef NDEBUG
    for (auto child : TermBase::m_children) {
      //m_state is atomic, and a state will never transition back from evaluated
      //so it is safe to check if children are evaluated without locking
      assert(child->m_state == TermBase::EVALUATED);
    }
#endif
    
    std::lock_guard<std::mutex> lock(TermBase::m_mutex);

    if (TermBase::m_state == TermBase::UNSTARTED) {
      Typed::m_future =
        m_calculation(m, cv, this, done, TermBase::m_children);
      TermBase::m_state = TermBase::PENDING;
    }
  }

  virtual void clear() {
    std::lock_guard<std::mutex> lock(TermBase::m_mutex);
    if (clear_data(Typed::m_value))
      TermBase::m_state = TermBase::UNSTARTED;
  }

  virtual ~Term() {}
private:
  calculation_type m_calculation;
};

template <typename VALUE>
class TermList : public TermValue<std::vector<VALUE>> {
public:
  using Typed = TermValue<std::vector<VALUE>>;

  TermList() : Typed(std::vector<VALUE>()) {
    TermBase::m_state = TermBase::UNSTARTED;
  }
  TermList(TermList<VALUE> &&other) 
    : Typed(std::move(other)) {}
  
  template <typename SUBTERM>
  void push_back(SUBTERM &&subterm) {
    TermBase::add_child(std::forward<SUBTERM>(subterm));
  }
  
  TermValue<VALUE>& operator[](const size_t index) {
    return *(TermValue<VALUE>*)TermBase::m_children[index];
  }

  template <typename CALCULATION>
  Term<CALCULATION>& at(const size_t index) {
    return *(Term<CALCULATION>*)TermBase::m_children[index];
  }

  virtual void apply(std::mutex &m, std::condition_variable &cv, TermBase *&done) {
#ifndef NDEBUG
    for (auto child : TermBase::m_children) {
      //m_state is atomic, and a state will never transition back from evaluated
      //so it is safe to check if children are evaluated without locking
      assert(child->m_state == TermBase::EVALUATED);
    }
#endif
    
    std::lock_guard<std::mutex> lock(TermBase::m_mutex);

    if (TermBase::m_state == TermBase::UNSTARTED) {
      TermBase::m_state = TermBase::PENDING;
    }

    Typed::m_future = ThreadPoolSingleton::Instance()->enqueue(
      [&m, &cv, this, &done]() {
        typename Typed::value_type ret;
        for (auto child : TermBase::m_children)
          ret.push_back(**static_cast<TermValue<VALUE>*>(child));
          
        {
          std::unique_lock<std::mutex> lk(m);
          while (done != NULL) {
            cv.wait(lk);
            if (done != NULL) {
              lk.unlock();
              cv.notify_one();
              lk.lock();
            }
          }
          done = this;
          lk.unlock();
        }
        cv.notify_one();
        return ret;
      });
  }

  virtual void clear() {
    std::lock_guard<std::mutex> lock(TermBase::m_mutex);
    if (clear_data(Typed::m_value))
      TermBase::m_state = Typed::UNSTARTED;
  }

  virtual ~TermList() {}
};

}
}

#endif
