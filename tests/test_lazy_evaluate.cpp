#define BOOST_TEST_MODULE test_lazy_evaluate
#include <boost/test/unit_test.hpp>

#include "LazyEvaluate/Term.h"
#include "LazyEvaluate/Calculation.h"
#include <thread>
#include <iostream>

struct adder {
  adder(){}
  int operator()(const int &a, const int &b) {
    std::cout << "Starting add " << a << " + " << b << std::endl;
    std::this_thread::sleep_for (std::chrono::seconds(1));
    return a + b;
  }
};

int bare_adder(int a, int b) {
  std::cout << "Starting add " << a << " + " << b << std::endl;
  std::this_thread::sleep_for (std::chrono::seconds(1));
  return a + b;
}

using namespace libraries_oqton::LazyEvaluate;

BOOST_AUTO_TEST_CASE(simple_calc) {
  ThreadPoolCalculation<adder> calc;
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
  Term<adder> eleven;
  
  eleven.terms(five, six);
  int result = *eleven;
  BOOST_REQUIRE_EQUAL(result, 11);
}

BOOST_AUTO_TEST_CASE(term_from_temporary) {
  TermValue five(5);
  TermValue six(6);
  // This should work, but seems to trigger a bug in g++
  //Term eleven(adder());

  // So instead declare terms from temporaries like this
  auto eleven = Term(adder());
  
  eleven.terms(five, six);
  int result = *eleven;
  BOOST_REQUIRE_EQUAL(result, 11);
}

BOOST_AUTO_TEST_CASE(term_from_instance) {
  TermValue five(5);
  TermValue six(6);
  adder a;
  Term eleven(a);
  
  eleven.terms(five, six);
  int result = *eleven;
  BOOST_REQUIRE_EQUAL(result, 11);
}

BOOST_AUTO_TEST_CASE(term_from_func_ptr) {
  TermValue five(5);
  TermValue six(6);
  Term eleven(&bare_adder);
  
  eleven.terms(five, six);
  int result = *eleven;
  BOOST_REQUIRE_EQUAL(result, 11);
}

BOOST_AUTO_TEST_CASE(term_from_lambda) {
  TermValue five(5);
  TermValue six(6);
  auto lam = [](int a, int b) {
    return a + b;
  };
  Term eleven(lam);
  
  eleven.terms(five, six);
  int result = *eleven;
  BOOST_REQUIRE_EQUAL(result, 11);
}

BOOST_AUTO_TEST_CASE(term_from_lambda_temp) {
  TermValue five(5);
  TermValue six(6);
  Term eleven([](int a, int b) { return a + b; });
  
  eleven.terms(five, six);
  int result = *eleven;
  BOOST_REQUIRE_EQUAL(result, 11);
}

BOOST_AUTO_TEST_CASE(inline_subterms) {
  TermValue five(5);
  TermValue six(6);
  Term eleven([](int a, int b) { return a + b; },
              five, six);
  
  int result = *eleven;
  BOOST_REQUIRE_EQUAL(result, 11);
}

struct adder_coef{
  adder_coef() {}
  adder_coef(const int coef) : m_coef(coef) {}
  int m_coef;

  int operator()(int a, int b) {
    std::cout << "Starting coefficient add " << a << " + " << b << std::endl;
    std::this_thread::sleep_for (std::chrono::seconds(1));
    return (a + b) * m_coef;
  }
};

BOOST_AUTO_TEST_CASE(term_set_calc_after) {
  TermValue five(5);
  TermValue six(6);

  adder_coef ac(2);
  Term<adder_coef> twentytwo;
  twentytwo.calculation(ac);
  twentytwo.terms(five, six);

  BOOST_REQUIRE_EQUAL(*twentytwo, 22);
}

BOOST_AUTO_TEST_CASE(term_set_calc_after_move) {
  TermValue five(5);
  TermValue six(6);

  Term<adder_coef> twentytwo;
  twentytwo.calculation(adder_coef(2));
  twentytwo.terms(five, six);

  BOOST_REQUIRE_EQUAL(*twentytwo, 22);
}

BOOST_AUTO_TEST_CASE(term_set_calc_after_emplace) {
  TermValue five(5);
  TermValue six(6);

  Term<adder_coef> twentytwo;
  twentytwo.calculation(2);
  twentytwo.terms(five, six);

  BOOST_REQUIRE_EQUAL(*twentytwo, 22);
}

BOOST_AUTO_TEST_CASE(two_level_term) {

  std::cout << "Setting up term" << std::endl;
  TermValue five(5);
  TermValue six(6);
  TermValue seven(7);
  TermValue eight(8);

  Term<adder> eleven;
  eleven.terms(five, six);

  Term<adder> fifteen;
  fifteen.terms(seven, eight);

  Term<adder> twentysix;
  twentysix.terms(eleven, fifteen);  

  std::cout << "Evaluating term" << std::endl;
  BOOST_REQUIRE_EQUAL(*twentysix, 26);
  std::cout << "Evaluated term" << std::endl;
}

BOOST_AUTO_TEST_CASE(diamond_term) {

  std::cout << "Setting up term" << std::endl;
  TermValue five(5);
  TermValue eight(8);
  TermValue two(2);
  TermValue four(4);

  Term<adder> six;
  six.terms(two, four);

  Term<adder> eleven;
  eleven.terms(five, six);

  Term<adder> fourteen;
  fourteen.terms(six, eight);

  Term<adder> twentyfive;
  twentyfive.terms(eleven, fourteen);  

  std::cout << "Evaluating term" << std::endl;
  BOOST_REQUIRE_EQUAL(*twentyfive, 25);
  std::cout << "Evaluated term" << std::endl;
}

BOOST_AUTO_TEST_CASE(term_type_check) {
  TermValue five(5);
  TermValue six(6);

  Term<adder> intint1;
  intint1.terms(five, six);

  TermValue str(std::string("Something"));
  Term<adder> intint2;
  TermValue seven(7);

  //Should fail to compile using term type check
  //uncomment to test term type checking
  //intint2.terms(five, str);

  //Should fail to compile using term count check
  //uncomment to test term count
  //intint2.terms(five, six, seven);

  TermValue em('m');
  //should compile because a char is convertable to an int
  intint2.terms(five, em);
}

struct sum_list {
  int operator()(std::vector<int> values) {
    std::cout << "Summing (" << values.size() << "): ";
    for (auto value : values)
      std::cout << value << ", ";
    std::cout << std::endl;
    
    int sum = 0;
    for (const int &value : values)
      sum += value;

    std::this_thread::sleep_for (std::chrono::seconds(1));
    return sum;
  }
};
    

BOOST_AUTO_TEST_CASE(simple_term_list) {
  TermList<int> subterms;

  TermValue one(1);
  TermValue two(2);
  
  subterms.push_back(TermValue(5));
  subterms.push_back(TermValue(6));
  subterms.push_back(Term<adder>());
  subterms.push_back(Term<adder>());
  subterms.push_back(Term<adder>());
  auto &last = subterms.push_back(Term<adder>());

  subterms.at<adder>(2).terms(one, two);
  subterms.at<adder>(3).terms(subterms[0], subterms[1]);
  subterms.at<adder>(4).terms(subterms[2], subterms[1]);
  last.terms(subterms[4], subterms[2]);

  Term<sum_list> sum;
  sum.terms(subterms);

  BOOST_REQUIRE_EQUAL(*sum,
                      5 + 6 +
                      (1 + 2) +
                      (5 + 6) +
                      ((1 + 2) + 6) +
                      (((1 + 2) + 6) + (1 + 2)));
  BOOST_REQUIRE_EQUAL(subterms.no_eval_access().size(), 0);
  BOOST_REQUIRE_EQUAL(subterms->size(), 6);
}

BOOST_AUTO_TEST_CASE(test_list_in_order) {
  TermList<int> subterms;

  subterms.push_back(TermValue(1));
  subterms.push_back(TermValue(1));
  for (size_t i = 0; i < 10; ++i) 
    subterms.push_back(Term<adder>());

  for (auto i = subterms.begin() + 2; i != subterms.end(); ++i) {
    i->as<adder>().terms(*(i - 1), *(i - 2));
  }

  auto fib = *subterms;
  BOOST_REQUIRE_EQUAL(fib[2], 2);
  BOOST_REQUIRE_EQUAL(fib[3], 3);
  BOOST_REQUIRE_EQUAL(fib[4], 5);
  BOOST_REQUIRE_EQUAL(fib[5], 8);
  BOOST_REQUIRE_EQUAL(fib[6], 13);
  BOOST_REQUIRE_EQUAL(fib[7], 21);
  BOOST_REQUIRE_EQUAL(fib[8], 34);
  BOOST_REQUIRE_EQUAL(fib[9], 55);
}

BOOST_AUTO_TEST_CASE(test_term_return) {
  Term<adder> top;
  auto [a, b] = top.terms(Term<adder>(), Term<adder>());

  a.terms(TermValue(5), TermValue(6));
  b.terms(TermValue(1), TermValue(2));

  BOOST_REQUIRE_EQUAL(*top, 14);
}

  
