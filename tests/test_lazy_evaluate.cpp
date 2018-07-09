#define BOOST_TEST_MODULE test_lazy_evaluate
#include <boost/test/unit_test.hpp>

#include "LazyEvaluate/Term.h"
#include "LazyEvaluate/Calculation.h"
#include <thread>
#include <iostream>

struct func {
  int operator()(int a, int b) {
    std::cout << "Starting add " << a << " + " << b << std::endl;
    std::this_thread::sleep_for (std::chrono::seconds(1));
    return a + b;
  }
};

using namespace libraries_oqton::LazyEvaluate;

BOOST_AUTO_TEST_CASE(simple_calc) {
  Calculation<func> calc;
  std::mutex m;
  std::condition_variable cv;
  TermValue one(5);
  TermValue two(6);
  TermValue me(1);
  TermBase *controller = &me;
  TermBase *done = NULL;
  auto fut = calc(m, cv, controller, done, std::vector<TermBase*>{&one, &two});
  int result = fut.get();
  BOOST_REQUIRE_EQUAL(result, 11);
}

BOOST_AUTO_TEST_CASE(simple_compound_term) {
  TermValue five(5);
  TermValue six(6);
  Term<Calculation<func>> eleven;
  
  eleven.terms(five, six);
  int result = *eleven;
  BOOST_REQUIRE_EQUAL(result, 11);
}

BOOST_AUTO_TEST_CASE(two_level_term) {

  std::cout << "Setting up term" << std::endl;
  TermValue five(5);
  TermValue six(6);
  TermValue seven(7);
  TermValue eight(8);

  Term<Calculation<func>> eleven;
  eleven.terms(five, six);

  Term<Calculation<func>> fifteen;
  fifteen.terms(seven, eight);

  Term<Calculation<func>> twentysix;
  twentysix.terms(eleven, fifteen);  

  std::cout << "Evaluating term" << std::endl;
  BOOST_REQUIRE_EQUAL(*twentysix, 26);
  std::cout << "Evaluated term" << std::endl;
}
