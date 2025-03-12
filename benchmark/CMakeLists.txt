cmake_minimum_required(VERSION 3.22)

# Benchmark
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "")
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "")
add_subdirectory(../external/googlebenchmark googlebenchmark EXCLUDE_FROM_ALL)

add_executable(bench benchmark.cpp)
target_compile_features(bench PRIVATE cxx_std_20)
target_compile_options(bench PRIVATE -O3 -g -march=native -DNDEBUG)
target_link_libraries(bench PRIVATE benchmark::benchmark_main)
target_link_libraries(bench PUBLIC main_lib)
target_link_libraries(bench PUBLIC Boost::atomic)

add_custom_command(OUTPUT benchmark.json
                COMMAND bench
                ARGS --benchmark_out=benchmark.json --benchmark_out_format=json
                DEPENDS bench
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                COMMENT "Running benchmark"
                VERBATIM)
                
add_custom_command(OUTPUT plots.pdf
                COMMAND Rscript "${PROJECT_SOURCE_DIR}/eval/make_plots.r"
                DEPENDS "${PROJECT_SOURCE_DIR}/eval/make_plots.r" benchmark.json
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                COMMENT "Generating plots"
                VERBATIM)
add_custom_target(plots DEPENDS plots.pdf)