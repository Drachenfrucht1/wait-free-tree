#include <benchmark/benchmark.h>

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <random>
#include <thread>

#include "implementation/concurrent_tree.hpp"

//alpha is in percent
template <int min = 1, int max = 1'000'000, int alpha = 50, int range_size = 100, int ops_per_thread = 20'000, bool rebuild = true>
void BM_tree(benchmark::State& state) {
  const unsigned int num_threads = static_cast<unsigned int>(state.range(0));
  std::default_random_engine rng(num_threads);
  std::uniform_int_distribution<> dist(min, max);
  std::uniform_int_distribution<> opdist(1, 4);

  std::vector<int> data(ops_per_thread * num_threads);
  std::vector<int> ops(ops_per_thread * num_threads);
  std::vector<int> prefill((max-min * alpha) / 100);
  std::generate(data.begin(), data.end(), [&] { return dist(rng); });
  std::generate(ops.begin(), ops.end(), [&] { return opdist(rng); });
  std::generate(prefill.begin(), prefill.end(), [&] { return dist(rng); });

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    ConcurrentTree<int> tree(prefill, num_threads);
    state.ResumeTiming();
    {
      for(unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i] {
          for (unsigned int j = 0; j < ops_per_thread; ++j) {
            int op = ops[i * ops_per_thread + j];
            int value = data[i * ops_per_thread + j];
            bool b = false;
            std::uint32_t result;
            switch (op)
            {
            case 1:
              tree.insert(value, i);
              break;
            case 2:
              tree.remove(value, i);
              break;
            case 3:
              b = tree.lookup(value, i);
              break;
            case 4:
              result = tree.range_count(value, value+range_size, i);
              break;
            default:
              break;
            }
          }
        });
      }
      for (auto &th: threads){
        th.join();
      }
      benchmark::ClobberMemory();
    }
  }
}

template <int min = 1, int max = 1'000'000, int alpha = 50, int ops_per_thread = 20'000, bool rebuild = true>
void BM_insertremove(benchmark::State& state) {
  const unsigned int num_threads = static_cast<unsigned int>(state.range(0));
  std::default_random_engine rng(num_threads);
  std::uniform_int_distribution<> dist(min, max);
  std::uniform_int_distribution<> opdist(1, 2);

  std::vector<int> data(ops_per_thread * num_threads);
  std::vector<int> ops(ops_per_thread * num_threads);
  std::vector<int> prefill((max-min * alpha) / 100);
  std::generate(data.begin(), data.end(), [&] { return dist(rng); });
  std::generate(ops.begin(), ops.end(), [&] { return opdist(rng); });
  std::generate(prefill.begin(), prefill.end(), [&] { return dist(rng); });

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    ConcurrentTree<int> tree(prefill, num_threads);
    state.ResumeTiming();
    {
      for(unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i] {
          for (unsigned int j = 0; j < ops_per_thread; ++j) {
            int op = ops[i * ops_per_thread + j];
            int value = data[i * ops_per_thread + j];
            bool b = false;
            std::uint32_t result;
            switch (op)
            {
            case 1:
              tree.insert(value, i);
              break;
            case 2:
              tree.remove(value, i);
              break;
            default:
              break;
            }
          }
        });
      }
      for (auto &th: threads){
        th.join();
      }
      benchmark::ClobberMemory();
    }
  }
}

//all template parameters except for ops_per_thread are ignored, but still present for the R script
template <int min = 1, int max = 1'000'000, int alpha = 50, int ops_per_thread = 20'000, bool rebuild = true>
void BM_special(benchmark::State& state) {
  const unsigned int num_threads = static_cast<unsigned int>(state.range(0));
  std::default_random_engine rng(num_threads);
  std::uniform_int_distribution<> dist;

  std::vector<int> data(ops_per_thread * num_threads);
  std::vector<int> prefill(1'000'000);
  std::generate(data.begin(), data.end(), [&] { return dist(rng); });
  std::generate(prefill.begin(), prefill.end(), [&] { return dist(rng); });

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    ConcurrentTree<int> tree(prefill, num_threads);
    state.ResumeTiming();
    {
      for(unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i] {
          for (unsigned int j = 0; j < ops_per_thread; ++j) {
            int value = data[i * ops_per_thread + j];
            tree.insert(value, i);
          }
        });
      }
      for (auto &th: threads){
        th.join();
      }
      benchmark::ClobberMemory();
    }
  }
}

template <int min = 1, int max = 1'000'000, int alpha = 50, int ops_per_thread = 20'000, bool rebuild = true>
void BM_norange(benchmark::State& state) {
  const unsigned int num_threads = static_cast<unsigned int>(state.range(0));
  std::default_random_engine rng(num_threads);
  std::uniform_int_distribution<> dist(min, max);
  std::uniform_int_distribution<> opdist(1, 3);

  std::vector<int> data(ops_per_thread * num_threads);
  std::vector<int> ops(ops_per_thread * num_threads);
  std::vector<int> prefill((max-min * alpha) / 100);
  std::generate(data.begin(), data.end(), [&] { return dist(rng); });
  std::generate(ops.begin(), ops.end(), [&] { return opdist(rng); });
  std::generate(prefill.begin(), prefill.end(), [&] { return dist(rng); });

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    ConcurrentTree<int> tree(prefill, num_threads);
    state.ResumeTiming();
    {
      for(unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i] {
          for (unsigned int j = 0; j < ops_per_thread; ++j) {
            int op = ops[i * ops_per_thread + j];
            int value = data[i * ops_per_thread + j];
            bool b = false;
            std::uint32_t result;
            switch (op)
            {
            case 1:
              tree.insert(value, i);
              break;
            case 2:
              tree.remove(value, i);
              break;
            case 3:
              b = tree.lookup(value, i);
              break;
            default:
              break;
            }
          }
        });
      }
      for (auto &th: threads){
        th.join();
      }
      benchmark::ClobberMemory();
    }
  }
}

template <int min = 1, int max = 1'000'000, int alpha = 50, int ops_per_thread = 20'000, bool rebuild = true>
void BM_lookup(benchmark::State& state) {
  const unsigned int num_threads = state.range(0);
  std::default_random_engine rng(num_threads);
  std::uniform_int_distribution<> dist(min, max);
  std::uniform_int_distribution<> opdist(1, 4);

  std::vector<int> data(ops_per_thread * num_threads);
  std::vector<int> prefill((max-min * alpha) / 100);
  std::generate(data.begin(), data.end(), [&] { return dist(rng); });
  std::generate(prefill.begin(), prefill.end(), [&] { return dist(rng); });

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    ConcurrentTree<int> tree(prefill, num_threads);
    state.ResumeTiming();

    for(unsigned int i = 0; i < num_threads; ++i) {
      threads.emplace_back([&, i] {
        for (int j = 0; j < ops_per_thread; ++j) {
          int value = data[i * ops_per_thread + j];
          bool b = tree.lookup(value, i);
        }
      });
    }

    for (auto &th: threads){
      th.join();
    }
  }
}

constexpr int min_threads = 1;
constexpr int max_threads = 16;
constexpr int iterations = 5;

BENCHMARK(BM_insertremove<1, 1000000, 50, 25000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads); // from paper
BENCHMARK(BM_special<1, 1000000, 50, 25000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads); // from paper
BENCHMARK(BM_lookup<1, 1000000, 50, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads); // from paper

BENCHMARK(BM_lookup<1, 1000000, 25, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_lookup<1, 1000000, 75, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);

BENCHMARK(BM_tree<1, 1000000, 25, 100, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_tree<1, 1000000, 50, 100, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_tree<1, 1000000, 75, 100, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);

BENCHMARK(BM_tree<1, 1000000, 50, 1000, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_tree<1, 1000000, 50, 10000, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);

BENCHMARK(BM_norange<1, 1000000, 25, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_norange<1, 1000000, 50, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_norange<1, 1000000, 75, 50000, true>)->RangeMultiplier(2)->Range(min_threads, max_threads);

// //NO REBUILD FROM HERE

BENCHMARK(BM_insertremove<1, 1000000, 50, 25000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads); // from paper
BENCHMARK(BM_special<1, 1000000, 50, 25000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads); // from paper
BENCHMARK(BM_lookup<1, 1000000, 50, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads); // from paper

BENCHMARK(BM_lookup<1, 1000000, 25, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_lookup<1, 1000000, 75, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);

BENCHMARK(BM_tree<1, 1000000, 25, 100, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_tree<1, 1000000, 50, 100, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_tree<1, 1000000, 75, 100, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);

BENCHMARK(BM_tree<1, 1000000, 50, 1000, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_tree<1, 1000000, 50, 10000, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);

BENCHMARK(BM_norange<1, 1000000, 25, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_norange<1, 1000000, 50, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);
BENCHMARK(BM_norange<1, 1000000, 75, 50000, false>)->RangeMultiplier(2)->Range(min_threads, max_threads);