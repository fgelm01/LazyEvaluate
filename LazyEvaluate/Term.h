#ifndef OQTON_LAZY_EVALUATE_TERM_H
#define OQTON_LAZY_EVALUATE_TERM_H
#include "lang_utils/tuple.h"
#include <type_traits>

namespace libraries_oqton {
namespace LazyEvaluate {

class TermBase {}

template <typename RESULT>
class Term : public TermBase {
private:
  using state_t = enum { UNSTARTED, PENDING, EVALUATED };
  using starter_t = typename std::function<std::future<RESULT>()>;

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
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state == UNSTARTED) {
      m_future = m_starter();
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
  std::tuple<std::reference_wrapper<ELEMENTS>...> m_elements;
  
  std::future<RESULT> m_future;
  starter_t m_starter;

  std::atomic<state_t> m_state;
  std::mutex m_mutex;
  RESULT m_value;
};

}
}

#endif
