cmake_minimum_required(VERSION 3.22)

# Tests
include(CTest)
if(BUILD_TESTING)

    add_executable(correctness tests.cpp)
    target_compile_features(correctness PRIVATE cxx_std_20)
    target_link_libraries(correctness PRIVATE main_lib)
    target_link_libraries(correctness PUBLIC Boost::atomic)


    add_executable(correctness_q conditional_q_test.cpp)
    target_compile_features(correctness_q PRIVATE cxx_std_20)
    target_link_libraries(correctness_q PRIVATE main_lib)
    target_link_libraries(correctness_q PUBLIC Boost::atomic)

    add_executable(correctness_msq queue.cpp)
    target_compile_features(correctness_msq PRIVATE cxx_std_20)
    target_link_libraries(correctness_msq PRIVATE main_lib)
    target_link_libraries(correctness_msq PUBLIC Boost::atomic)
    
endif()