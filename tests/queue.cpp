#include "implementation/waitfree_queue.hpp"

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>


int main() {
    const auto num_threads = std::thread::hardware_concurrency();
    const int initial_size = 500;
    constexpr auto num_elements = 1'000'000;

    WaitFreeQueue<int> queue(num_threads);
    std::vector<std::atomic_char> seen(num_elements + initial_size);
    for (int i = 1; i <= initial_size; ++i) {
        // std::clog << "Inserting " << i << std::endl;
        queue.push(i, 0);
    }

    std::clog << "Using " << num_threads << " threads" << std::endl;
    std::atomic_bool success = true;
    {
        std::vector<std::jthread> threads;
        threads.reserve(num_threads);
        const auto elem_per_thread = num_elements / num_threads;
        for (auto i = 0u; i < num_threads; ++i) {
            threads.emplace_back([&, i] {
                for (int j = 0; j < elem_per_thread; ++j) {
                    auto v = queue.pop(i);
                    if (v < 0 || v > num_elements + initial_size) {
                        std::cerr << "Error: Popped invalid value\n";
                        success = false;
                        return;
                    }
                    if (seen[v - 1].fetch_add(1, std::memory_order_relaxed) >
                        0) {
                        std::cerr << "Error: Duplicate value '" << v
                                  << "' seen by thread " << i << ".\n";
                        success = false;
                        return;
                    }
                    queue.push(i * elem_per_thread + j + initial_size + 1, i);
                }
            });
        }
    }
    if (!success) {
        std::clog << "Test failed\n";
        return 1;
    }
    std::clog << "Test successful\n";
    return 0;
}
