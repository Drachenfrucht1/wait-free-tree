cmake_minimum_required (VERSION 3.22)

project(wait-free-tree VERSION 1.0 LANGUAGES CXX)
# set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

# Boost atomics
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if (DEFINED ENV{CI})
  set(Boost_USE_STATIC_LIBS ON)
else()
endif()

find_package(Boost REQUIRED COMPONENTS atomic)

# Main library
add_library(main_lib INTERFACE)
target_sources(main_lib INTERFACE
  ./implementation/concurrent_tree.hpp
  ./implementation/hazard_pointers.hpp
  ./implementation/conditional_hazard_pointers.hpp
  ./implementation/conditional_q.hpp
  ./implementation/tree_internals.hpp
  ./implementation/tuple_queue.hpp
  ./implementation/waitfree_queue.hpp
)
target_include_directories(main_lib INTERFACE ./)
target_link_libraries(main_lib INTERFACE Threads::Threads)
target_link_libraries(main_lib INTERFACE Boost::atomic)
target_compile_features(main_lib INTERFACE cxx_std_20)

add_subdirectory(benchmark)
add_subdirectory(tests)