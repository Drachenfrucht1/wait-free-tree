#include "implementation/concurrent_tree.hpp"

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <numeric>
#include <random>

bool insert_test() {
  const auto num_threads = std::thread::hardware_concurrency();
  constexpr auto num_elements = 16000;

  ConcurrentTree<int> tree(num_threads);
  tree.print_atomic_capabilities();

  std::clog << "Using " << num_threads << " threads" << std::endl;
  std::vector<int> data(num_elements);
  std::iota(data.begin(), data.end(), 1);
  std::mt19937 g(42);
  std::shuffle(data.begin(), data.end(), g);

  std::atomic_bool success = true;
  std::atomic<std::size_t> finished = 0;
  {
      std::vector<std::jthread> threads;
      threads.reserve(num_threads);
      const auto elem_per_thread = num_elements / num_threads;
      for (auto i = 0u; i < num_threads; ++i) {
          threads.emplace_back([&, i] {
              for (unsigned int j = 0; j < elem_per_thread; ++j) {
                  if (!tree.insert(data[i * elem_per_thread + j], i))
                    std::clog << "Failed to insert " << data[i * elem_per_thread + j] << " " << i << '\n';
                  // else
                  //   std::clog << "inserted " << i * elem_per_thread + j + 1 << '\n';
              }
              std::size_t a = finished.fetch_add(1, std::memory_order_relaxed);
              std::clog << i << " finished " << (a+1) << std::endl;
          });
      }
  }
  std::clog << "All values in tree" << std::endl;
  int missing = 0;
  for (auto i : data) {
    if (!tree.lookup(i, 0)) {
      std::clog << "Failed to lookup " << i << std::endl;
      success = false;
      ++missing;
    }
  }
  std::clog << missing << " values missing\n"; 
  std::clog << "Insert Test ended\n";
  return success;
}

bool remove_test() {
  const auto num_threads = std::thread::hardware_concurrency();
  constexpr auto num_elements = 16000;

  ConcurrentTree<int> tree(num_threads);

  std::clog << "Using " << num_threads << " threads" << std::endl;
  std::vector<int> data(num_elements);
  std::iota(data.begin(), data.end(), 1);
  std::mt19937 g(42);
  std::shuffle(data.begin(), data.end(), g);

  std::atomic_bool success = true;
  {
      std::vector<std::jthread> threads;
      threads.reserve(num_threads);
      const auto elem_per_thread = (num_elements/2) / num_threads;
      for (auto i = 0u; i < num_threads; ++i) {
          threads.emplace_back([&, i] {
              for (unsigned int j = 0; j < elem_per_thread; ++j) {
                  if (!tree.insert(data[i * elem_per_thread + j], i))
                    std::clog << "Failed to insert " << data[i * elem_per_thread + j] << std::endl;
                  // else
                  //   std::clog << "inserted " << i * elem_per_thread + j + 1 << std::endl;
              }
          });
      }
  }

  int missing = 0;
  for (unsigned int i = 0; i < num_elements/2; ++i) {
    if (!tree.lookup(data[i], 0)) {
      std::clog << "Failed to lookup " << data[i] << std::endl;
      success = false;
      ++missing;
    }
  }
  std::clog << missing << " values missing\n"; 

  {
    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    const auto elem_per_thread = num_elements / num_threads;
    for (auto i = 0u; i < num_threads; ++i) {
        threads.emplace_back([&, i] {
            for (unsigned int j = 0; j < elem_per_thread; ++j) {
                if (i < num_threads/2) {
                  tree.remove(data[i* elem_per_thread + j], i);
                } else {
                  if (!tree.insert(data[i * elem_per_thread + j], i))
                    std::clog << "Failed to insert " << data[i * elem_per_thread + j] << std::endl;
                  // else
                  //   std::clog << "inserted " << i * elem_per_thread + j + 1 << std::endl;
                }
            }
        });
    }
  }
  missing = 0;
  for (unsigned int i = 1; i < num_elements; ++i) {
    bool found = tree.lookup(data[i], 0);
    if ((found && i < num_elements/2) || (!found && i >= num_elements/2)) {
      std::clog << "Failed to(not) lookup " << data[i] << std::endl;
      success = false;
      ++missing;
    }
  }
  std::clog << missing << " values missing\n";

  {
    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    const auto elem_per_thread = (num_elements/2) / num_threads;
    for (auto i = 0u; i < num_threads; ++i) {
        threads.emplace_back([&, i] {
            for (unsigned int j = 0; j < elem_per_thread; ++j) {
                if (!tree.insert(data[i * elem_per_thread + j], i))
                  std::clog << "Failed to insert " << data[i * elem_per_thread + j] << std::endl;
                // else
                //   std::clog << "inserted " << i * elem_per_thread + j + 1 << std::endl;
            }
        });
    }
  }
  missing = 0;
  for (unsigned int i = 0; i < num_elements; ++i) {
    if (!tree.lookup(data[i], 0)) {
      std::clog << "Failed to lookup " << data[i] << std::endl;
      success = false;
      ++missing;
    }
  }
  std::clog << missing << " values missing\n"; 
  std::clog << "Finished Remove Test\n";
  return success;
}

bool range_test() {
  const auto num_threads = std::thread::hardware_concurrency();
  constexpr auto num_elements = 25000;

  

  std::vector<int> insert(num_elements+1);
  std::iota(insert.begin(), insert.end(), num_elements/2);
  ConcurrentTree<int> tree(insert, num_threads);
  std::mt19937 g(42);

  std::clog << "Using " << num_threads << " threads" << std::endl;
  std::vector<int> data(2*num_elements);
  std::iota(data.begin(), data.end(), 1);
  std::shuffle(data.begin(), data.end(), g);

  std::atomic_bool success = true;
  std::atomic<std::size_t> failed_ranges = 0;
  std::atomic<std::size_t> total_ranges = 0;
  std::atomic<std::size_t> failed_lookups = 0;
  std::atomic<std::size_t> total_lookups = 0;
  {
      std::vector<std::jthread> threads;
      threads.reserve(num_threads);
      const auto elem_per_thread = 2*num_elements / num_threads;
      for (auto i = 0u; i < num_threads; ++i) {
          threads.emplace_back([&, i] {
              for (unsigned int j = 0; j < elem_per_thread && i*elem_per_thread + j + 1 < num_elements; j += 2) {
                int l = data[i*elem_per_thread + j];
                int r = data[i*elem_per_thread + j + 1];
                if (l < r) {
                  total_ranges.fetch_add(1);
                  std::uint32_t should_result = 0;
                  if (l <= num_elements + num_elements/2 || r >= num_elements/2) {
                    int l2 = std::max(l, num_elements/2);
                    int r2 = std::min(r, num_elements + num_elements/2);
                    if (l2 <= r2)
                      should_result = static_cast<std::uint32_t>((r2-l2)+1);
                  }
                  std::uint32_t result = tree.range_count(l, r, i);
                  if (result != should_result) {
                    failed_ranges.fetch_add(1);
                    std::clog << "Wrong range count " << l << " " << r << " " << should_result << " " << result << std::endl;
                    success = false;
                  }
                } else {
                  bool b = tree.lookup(l, i); 
                  total_lookups.fetch_add(1);
                  if (b != (l >= num_elements/2 && l <= num_elements + num_elements/2)) {
                    failed_lookups.fetch_add(1);
                    std::clog << "Wrong lookup " << l << " " << b << std::endl;
                    success = false;
                  }
                  b = tree.lookup(r, i); 
                  total_lookups.fetch_add(1);
                  if (b != (r >= num_elements/2 && r <= num_elements + num_elements/2)) {
                    std::clog << "Wrong lookup " << r << " " << b << std::endl;
                    failed_lookups.fetch_add(1);
                    success = false;
                  }
                }
              }
          });
      }
  }
  std::clog << failed_ranges.load() << "/" << total_ranges.load() << " failed range queries\n";
  std::clog << failed_lookups.load() << "/" << total_lookups.load() << " failed range queries\n";
  std::clog << "Finished lookup/range_count Test\n";
  return success;
}

int main() {
  return !insert_test() | !remove_test() | !range_test();
}
