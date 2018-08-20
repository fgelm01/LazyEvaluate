project(LazyEvaluate)
cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package( Threads REQUIRED )
find_package( Boost REQUIRED COMPONENTS unit_test_framework )

link_libraries( ${CMAKE_THREAD_LIBS_INIT} )
link_libraries( ${Boost_LIBRARIES} )

include_directories(${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/submodules/CppLangUtils/include
  ${CMAKE_CURRENT_SOURCE_DIR}/submodules/ThreadPool
  ${Boost_INCLUDE_DIRS})

add_definitions(-DBOOST_TEST_DYN_LINK)

enable_testing()

file(GLOB TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
foreach (TEST_SRC ${TEST_SRCS})
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
  add_executable(${TEST_NAME} ${TEST_SRC})
  add_test(${TEST_NAME} ${TEST_NAME})
endforeach()
