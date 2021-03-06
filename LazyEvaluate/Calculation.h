#ifndef OQTON_LAZY_EVALUATE_CALCULATION_H
#define OQTON_LAZY_EVALUATE_CALCULATION_H

#include "Term.h"
#include "lang_utils/tuple.h"
#include "ThreadPoolSingleton.h"

namespace libraries_oqton {
namespace LazyEvaluate {

template <typename FUNC> struct function_traits;

template <typename RET, typename ...ARGS>
struct function_traits<RET(*)(ARGS...)>
    : public function_traits<RET(ARGS...)> { };
    
template <typename RET, typename ...ARGS>
struct function_traits<RET(ARGS...)> {
    using return_type = RET;
    using args_type = std::tuple<ARGS...>;
};

template <typename C, typename RET, typename ...ARGS>
struct function_traits<RET(C::*)(ARGS...)> {
    using return_type = RET;
    using args_type = std::tuple<ARGS...>;
};

template <typename C, typename RET, typename ...ARGS>
struct function_traits<RET(C::*)(ARGS...) const> {
    using return_type = RET;
    using args_type = std::tuple<ARGS...>;
};

template <typename FUNC>
struct function_traits {
private:
    using traits_type = function_traits<decltype(&FUNC::operator())>;
public:
    using return_type = typename traits_type::return_type;
    using args_type = typename traits_type::args_type;
};


template <typename ...ARGS>
void dummy(ARGS&& ...args) {}

template <size_t INDEX, typename TUP1, typename TUP2>
int test_elem_equivalency(TUP1 &&tup1, TUP2 &&tup2) {
  static_assert(std::is_convertible<
                typename std::tuple_element<INDEX,
                  typename std::decay<TUP1>::type>::type,
                typename std::tuple_element<INDEX,
                typename std::decay<TUP2>::type>::type>::value,
                "Term types do not match calculation parameters");
  return 0;
}

template <typename TUP1, typename TUP2, size_t ...INDEX>
void test_tup_equivalency_impl(TUP1 &&tup1, TUP2 &&tup2, std::index_sequence<INDEX...>) {
  dummy(test_elem_equivalency<INDEX>(std::forward<TUP1>(tup1),
                                     std::forward<TUP2>(tup2))...);
}

template <typename TUP1, typename TUP2>
void test_tup_equivalency(TUP1 &&tup1, TUP2 &&tup2) {
  static_assert(std::tuple_size<typename std::decay<TUP1>::type>::value ==
                std::tuple_size<typename std::decay<TUP2>::type>::value,
                "Incorrect number of terms for calculation");
  test_tup_equivalency_impl(std::forward<TUP1>(tup1),
                            std::forward<TUP2>(tup2),
                            std::make_index_sequence<
                              std::tuple_size<
                                typename std::decay<TUP1>::type>::value>());
}

template<typename T>
using id_method_t =
  decltype( std::declval<T&>().id() );

template <typename FUNC>
std::string func_id(FUNC &&func) {
  if constexpr (std::experimental::is_detected_v<id_method_t, FUNC>) {
    return func.id();
  }
  else {
    return std::string("unknown");
  }
}


template <typename FUNC>
class ThreadPoolCalculation :
    public CalculationBase<typename function_traits<FUNC>::return_type> {
public:
  using func_t = FUNC;
  using traits_t = function_traits<FUNC>;
  using args_t = typename lang_utils::transform_tuple_type<std::decay, typename traits_t::args_type>::type;
  using return_t = typename traits_t::return_type;
  using future_t = typename std::future<return_t>;

private:
  args_t terms_to_args_tup(const std::vector<TermBase*> &terms) {
    assert(terms.size() == std::tuple_size<args_t>::value);
    args_t result;
    lang_utils::foreach_tuple_i(
      [&terms](const size_t i, auto &arg) {
        TermBase* term_ptr = terms[i];
        auto &term = *static_cast<TermValue<typename std::decay<decltype(arg)>::type>*>(term_ptr);
        //arg = **(static_cast<Term<decltype(arg)>*>(terms[i]));
        arg = *term;
      }, result);
    return result;
  }
  
public:
  ThreadPoolCalculation(const FUNC &func = FUNC())
    : m_func(func) {}
  ThreadPoolCalculation(FUNC &&func)
    : m_func(std::move(func)) {}

  template <typename TERM>
  struct terms_to_values {
    using type = typename TERM::value_type;
  };
  
  template <typename ...TERMS>
  void check_terms(TERMS&& ...terms) {
    test_tup_equivalency(
       typename lang_utils::transform_tuple_type<terms_to_values, typename std::tuple<typename std::decay<TERMS>::type...>>::type(),
        args_t());
  }
  
  virtual future_t operator()(std::mutex &m,
                              std::condition_variable &done_cv,
                              std::condition_variable &ready_cv,
                              TermBase *controller, TermBase *&done,
                              const std::vector<TermBase*> &terms) {
    return ThreadPoolSingleton::Instance()->enqueue(
      [&m, &done_cv, &ready_cv, controller, &done, terms, this]() {
        auto ret = apply(m_func,
                         terms_to_args_tup(terms));
        {
          std::unique_lock<std::mutex> lk(m);
          while (done != NULL)
            ready_cv.wait(lk);
          done = controller;
          done_cv.notify_one();
        }
        return ret;
      });
  }
    
  std::string id() const {
    return func_id(m_func);
  }
private:
  func_t m_func;
};


}
}
#endif
