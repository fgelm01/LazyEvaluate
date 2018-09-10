#ifndef OQTON_LAZY_EVALUATE_TERM_H
#define OQTON_LAZY_EVALUATE_TERM_H
#include "ThreadPoolSingleton.h"
#include "lang_utils/tuple.h"
#include <type_traits>
#include <experimental/type_traits>
#include <future>
#include <set>
#include <boost/iterator/transform_iterator.hpp>

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

        for (TermBase* child : cur->m_children)
          child->m_parents.push_back(cur);

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
    std::condition_variable done_cv;
    std::condition_variable ready_cv;
    TermBase *done = NULL;
    //evaluate each undepended term, passing done condition, get evaluating list
    
    while (m_state != EVALUATED) {
      std::vector<TermBase*> finalizable;
      if (undependent.empty()) {
        {
          std::unique_lock<std::mutex> lk(m);
          if (done == NULL) {
            ready_cv.notify_one();
            done_cv.wait(lk);
          }
          finalizable.push_back(done);
          done = NULL;
        }
      }

      for (TermBase *next : undependent) {
        if (next->apply(m, done_cv, ready_cv, done))
          finalizable.push_back(next);
      }

      undependent.clear();
      
      for (auto cur : finalizable) {
        cur->finalize(); //force state change, should not block
        for (TermBase *parent : cur->m_parents) {
          if (parent->children_evaluated()) {
            undependent.push_back(parent);
          }
        }
        for (TermBase *child : cur->m_children) {
            if (child->parents_evaluated())
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

  virtual std::string id() const = 0;
  virtual bool apply(std::mutex &m,
                     std::condition_variable &done_cv,
                     std::condition_variable &ready_cv,
                     TermBase *&done) = 0;
  virtual void finalize() = 0;
  virtual void clear() = 0;
  void name(const std::string &name) { m_name = name; }

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
  }
  
  std::vector<TermBase*> m_children;
  std::vector<bool> m_allocated;
  std::vector<TermBase*> m_parents;
  std::atomic<state_t> m_state;
  std::mutex m_mutex;
  std::string m_name;
};

template <typename VALUE>
std::string value_id(VALUE) {
  return std::string("unknown");
}

inline std::string value_id(int) {
  return std::string("int");
}

inline std::string value_id(std::string) {
  return std::string("string");
}

inline std::string value_id(float) {
  return std::string("float");
}

template <typename RESULT>
class CalculationBase {
public:
  virtual std::future<RESULT> operator()(std::mutex&,
                                         std::condition_variable&,
                                         std::condition_variable&,
                                         TermBase*,
                                         TermBase*&,
                                         const std::vector<TermBase*>&) = 0;
};

template <typename FUNC> class ThreadPoolCalculation;


template <typename FUNC, template <typename> class CALC> class Term;

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

  virtual std::string id() const {
    return std::string("TermValue<") + value_id(value_type()) + std::string(">") +
      std::string(":") + TermBase::m_name;
  }
  
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
  virtual bool apply(std::mutex &m,
                     std::condition_variable &done_cv,
                     std::condition_variable &ready_cv,
                     TermBase *&done) { return true; }

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

  template <typename FUNC, template <typename> class CALC=ThreadPoolCalculation>
  Term<FUNC, CALC>& as() {
#ifdef NDEBUG
    return *static_cast<Term<FUNC, CALC>*>(this);
#else
    Term<FUNC, CALC>* term = dynamic_cast<Term<FUNC, CALC>*>(this);
      assert(term != nullptr);
      return *term;
#endif
  }

  void operator=(value_type &value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value = value;
    m_state = EVALUATED;
  }

  void operator=(value_type &&value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value = std::move(value);
    m_state = EVALUATED;
  }

  void set_default() {
    *this = value_type();
  }
  
  virtual void clear() { }
    
  virtual ~TermValue() {}
  
protected:
  std::future<value_type> m_future;
  value_type m_value;
};

template <typename FUNC,
          template <typename> class CALC=ThreadPoolCalculation>
class Term : public TermValue<typename CALC<FUNC>::return_t> {
public:
  using Me = Term<FUNC, CALC>;
  using Typed = TermValue<typename CALC<FUNC>::return_t>;
  using calculation_type = CALC<FUNC>;

  Term(Me &&other)
    : m_calculation(std::move(other.m_calculation)),
      Typed(std::move(other)) {}

  template <typename ...TERMS>
  Term(FUNC &func, TERMS&& ...subterms)
    : m_calculation(calculation_type(func)),
      Typed(typename Typed::value_type()) {
    TermBase::m_state = TermBase::UNSTARTED;
    terms(std::forward<TERMS>(subterms)...);
  }

  template <typename ...TERMS>
  Term(FUNC &&func, TERMS&& ...subterms)
    : m_calculation(calculation_type(std::move(func))),
      Typed(typename Typed::value_type()) {
    TermBase::m_state = TermBase::UNSTARTED;
    terms(std::forward<TERMS>(subterms)...);
  }

  Term()
    : m_calculation(calculation_type(FUNC())),
      Typed(typename Typed::value_type()) {
    TermBase::m_state = TermBase::UNSTARTED;
  }

  void terms() {}

  virtual std::string id() const {
    return std::string("Term<") + m_calculation.id() + std::string(">") +
      std::string(":") + TermBase::m_name;
  }
  
  template <typename ...TERMS>
  auto terms(TERMS&& ...terms) {
    assert(TermBase::m_children.empty());
    m_calculation.check_terms(terms...);
    (TermBase::add_child(std::forward<TERMS>(terms)), ...);

    return term_ref_tup<TERMS...>();
  }

  void calculation(const FUNC &func) {
    m_calculation = calculation_type(func);
  }

  void calculation(FUNC &&func) {
    m_calculation = calculation_type(std::move(func));
  }

  template <typename ...ARGS>
  void calculation(ARGS&& ...args) {
    m_calculation = calculation_type(FUNC(std::forward<ARGS>(args)...));
  }
  
  virtual bool apply(std::mutex &m,
                     std::condition_variable &done_cv,
                     std::condition_variable &ready_cv,
                     TermBase *&done) {
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
        m_calculation(m, done_cv, ready_cv, this, done, TermBase::m_children);
      TermBase::m_state = TermBase::PENDING;
    }

    return false;
  }

  virtual void clear() {
    std::lock_guard<std::mutex> lock(TermBase::m_mutex);
    if (clear_data(Typed::m_value))
      TermBase::m_state = TermBase::UNSTARTED;
  }

  virtual ~Term() {}
private:

  template <typename REF_TUP, size_t POSITION>
  typename std::tuple_element<POSITION, REF_TUP>::type& term_ref_impl() {
    return *static_cast<typename std::decay<typename std::tuple_element<POSITION, REF_TUP>::type>::type*>(TermBase::m_children[POSITION]);
  }
  
  template <typename REF_TUP, size_t ...INDICES>
  REF_TUP term_ref_tup_impl(std::index_sequence<INDICES...> index_sequence) {
    return REF_TUP(term_ref_impl<REF_TUP, INDICES>()...);
  }

  template <typename ...TERMS>
  auto term_ref_tup() {
    return term_ref_tup_impl<typename std::tuple<typename std::add_lvalue_reference<TERMS>::type...>>(std::make_index_sequence<std::tuple_size<std::tuple<TERMS...>>::value>());
  }

  calculation_type m_calculation;
};

// Deduction guide for Term.
// Makes `auto term = Term(x, y, z, w)` and `Term term(x, y, z, w)` work,
// even with clang.
template <typename FUNC, typename... TERMS>
Term(FUNC&&, TERMS&&...) -> Term<std::decay_t<FUNC>>;

//Only need this because there is something wrong with the declaration syntax
//auto myterm =
// No there isn't. That was just the most vexing parse. Also, clang and gcc
// apparently disagree about the implicit deduction guides. See above.
template <typename FUNC, template <typename> class CALC=ThreadPoolCalculation>
auto makeTerm(FUNC &&func) {
  return Term<FUNC, CALC>(std::forward<FUNC>(func));
}

template <typename VALUE>
class TermList : public TermValue<std::vector<VALUE>> {
  static TermValue<VALUE>& MakeTermElem(TermBase *base) {
    return *static_cast<TermValue<VALUE>*>(base);
  }
  
public:
  using Typed = TermValue<std::vector<VALUE>>;

  TermList() : Typed(std::vector<VALUE>()) {
    TermBase::m_state = TermBase::UNSTARTED;
  }
  TermList(TermList<VALUE> &&other) 
    : Typed(std::move(other)) {}

  virtual std::string id() const {
    return std::string("TermList<") + value_id(VALUE()) + std::string(">") +
      std::string(":") + TermBase::m_name;
  }
  
  template <typename SUBTERM>
  typename std::decay<SUBTERM>::type& push_back(SUBTERM &&subterm) {
    TermBase::add_child(std::forward<SUBTERM>(subterm));
    return *static_cast<typename std::decay<SUBTERM>::type*>(TermBase::m_children.back());
  }
  
  TermValue<VALUE>& operator[](const size_t index) {
    return *(TermValue<VALUE>*)TermBase::m_children[index];
  }

  template <typename FUNC, template <typename> class CALC=ThreadPoolCalculation>
  Term<FUNC, CALC>& at(const size_t index) {
    return (*this)[index].template as<FUNC, CALC>();
  }


  auto begin() {
    return boost::make_transform_iterator(TermBase::m_children.begin(),
                                          MakeTermElem);
  }

  auto end() {
    return boost::make_transform_iterator(TermBase::m_children.end(),
                                          MakeTermElem);
  }

  using iterator = decltype(boost::make_transform_iterator(TermBase::m_children.begin(),
                                                           MakeTermElem));

  virtual bool apply(std::mutex &m,
                     std::condition_variable &done_cv,
                     std::condition_variable &ready_cv,
                     TermBase *&done) {
#ifndef NDEBUG
    for (auto child : TermBase::m_children) {
      //m_state is atomic, and a state will never transition back from evaluated
      //so it is safe to check if children are evaluated without locking
      assert(child->m_state == TermBase::EVALUATED);
    }
#endif
    
    std::lock_guard<std::mutex> lock(TermBase::m_mutex);

    std::promise<typename Typed::value_type> p;
    Typed::m_future = p.get_future();
    typename Typed::value_type value;
    for (auto child : TermBase::m_children)
      value.push_back(**static_cast<TermValue<VALUE>*>(child));
    p.set_value(value);
    
    if (TermBase::m_state == TermBase::UNSTARTED) {
      TermBase::m_state = TermBase::PENDING;
    }

    /*Typed::m_future = ThreadPoolSingleton::Instance()->enqueue(
      [&m, &done_cv, &ready_cv, this, &done]() {
        typename Typed::value_type ret;
        for (auto child : TermBase::m_children)
          ret.push_back(**static_cast<TermValue<VALUE>*>(child));
          
        {
          std::unique_lock<std::mutex> lk(m);
          if (done != NULL)
            ready_cv.wait(lk);
          done = this;
        }
        done_cv.notify_one();
        return ret;
        });*/

    return true;
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
