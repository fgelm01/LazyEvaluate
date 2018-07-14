project(libraries_oqton_cpp)
cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH
${CMAKE_CURRENT_SOURCE_DIR}/../build-tools)

include(common)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/submodules/CppLangUtils/include
  ${CMAKE_CURRENT_SOURCE_DIR}/submodules/ThreadPool)

find_package( Threads )
link_libraries( ${CMAKE_THREAD_LIBS_INIT} )
global_use_third_party(boost 1.66.0)

enable_testing()
add_tests(TEST_SRCS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)
