#include "implementation/conditional_q.hpp"

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>

struct TestObj {
    std::uint32_t timestamp;
};
struct TestObjA {
    std::atomic<std::uint64_t> timestamp = 0;
    std::uint32_t value;
};

bool input_test() {
    const auto num_threads = std::thread::hardware_concurrency();
    constexpr auto num_elements = 1'000'000;

    std::vector<TestObj*> data(num_threads * num_elements);
    ConditionalQ<TestObj> queue(num_threads);

    for (unsigned int x = 0; x < num_threads; ++x) {
        for (unsigned int i = 0; i < num_elements; ++i) {
            data[x * num_elements + i] = new TestObj(i);
        }
    }

    std::clog << "Using " << num_threads << " threads" << std::endl;
    std::atomic_bool success = true;
    {
        std::vector<std::jthread> threads;
        threads.reserve(num_threads);
        for (auto i = 0u; i < num_threads; ++i) {
            threads.emplace_back([&, i] {
                for (int j = 1; j < num_elements; ++j) {
                    queue.push_if(data[i * num_elements + j], i);
                }
            });
        }
    }
    std::uint32_t last_seen = 0;
    TestObj* a = nullptr;
    while ((a = queue.peek(0)) != nullptr) {
        

        if (a->timestamp != last_seen+1) {
            std::clog << "Duplicate or wrong order\n";
            std::clog << a->timestamp << " " << last_seen;
            return false;
        }
        last_seen = a->timestamp;
        queue.pop_if(a->timestamp, 0);
    }

    for (unsigned int x = 0; x < num_threads; ++x) {
        for (unsigned int i = 0; i < num_elements; ++i) {
            delete data[x * num_elements + i];
        }
    }
    
    // queue.print_all();

    std::clog << "Order test successfull\n";
    return true;
}

bool removal_test() {
    const auto num_threads = std::thread::hardware_concurrency();
    constexpr auto num_elements = 1'000'000;

    ConditionalQ<TestObj> queue(num_threads);
    std::vector<TestObj*> objects;
    std::vector<std::atomic_char> seen(num_elements);
    for (int i = 1; i <= num_elements; ++i) {
        auto a = new TestObj(i);
        objects.emplace_back(a);
        queue.push_if(a, 0);
    }

    std::clog << "Using " << num_threads << " threads" << std::endl;
    std::atomic_bool success = true;
    {
        std::vector<std::jthread> threads;
        threads.reserve(num_threads);
        const auto elem_per_thread = num_elements / num_threads;
        for (auto i = 0u; i < num_threads; ++i) {
            threads.emplace_back([&, i] {
                TestObj* a = nullptr;
                while ((a = queue.peek(i)) != nullptr) {
                    if (a->timestamp < 0 || a->timestamp > num_elements) {
                        std::cerr << "Error: Popped invalid value\n";
                        success = false;
                    }
                    seen[a->timestamp - 1].fetch_add(1, std::memory_order_relaxed);
                    
                    queue.pop_if(a->timestamp, i);
                }
                // std::cerr << "Info: Popped nullptr\n";
            });

        }
    }

    // queue.print_all();

    for (int i = 0; i < num_elements; ++i) {
        delete objects[i];
        if (seen[i].load() == 0) {
            std::clog << i+1 << " missing\n";
            success = false;
        }
    }
    if (!success) {
        std::clog << "Removal Test failed\n";
        return false;
    }

    std::clog << "Removal Test successful\n";
    return true;
}

std::atomic<std::uint64_t> last_timestamp_ = 1;
std::vector<std::atomic<TestObjA*>> ops_(std::thread::hardware_concurrency());

void add_ops_to_root(ConditionalQ<TestObjA>& q, std::size_t max_threads_, std::size_t tid) {
    std::vector<TestObjA*> to_insert;
    std::uint64_t own_timestamp = 0;
    std::uint64_t new_timestamp = last_timestamp_.fetch_add(1);
    if (ops_[tid].load()->timestamp.compare_exchange_strong(own_timestamp, new_timestamp)) { //this op can only be freed by this thread -> no hp necc
      own_timestamp = new_timestamp;
    }
    to_insert.push_back(ops_[tid].load());
    for (std::size_t i = 0; i < max_threads_; ++i) {
      TestObjA* a = ops_[i].load();
      if (a == nullptr)
        continue;
      if (a == ops_[i].load()) {
        if (a->timestamp == 0) {
          std::size_t check_timestamp = 0;
          new_timestamp = last_timestamp_.fetch_add(1);
          if(!a->timestamp.compare_exchange_strong(check_timestamp, new_timestamp)) {
            if (check_timestamp < own_timestamp) {
                to_insert.push_back(a);
            }
          }
        } else if (a->timestamp < own_timestamp)
          to_insert.push_back(a);
      }
    }
    std::sort(to_insert.begin(), to_insert.end(), [](TestObjA* a, TestObjA* b) {return a->timestamp < b->timestamp;});

    for (auto a : to_insert) {
    //   std::clog << "Inserting " << a->value << " with timestamp " << a->timestamp << std::endl;
      q.push_if(a, tid);
    }
  }

bool root_input_test() {
    const auto num_threads = std::thread::hardware_concurrency();
    constexpr auto num_elements = 1'000'000;
    std::atomic<std::size_t> next_value = 1;

    std::vector<TestObjA*> data(num_threads * num_elements);
    ConditionalQ<TestObjA> queue(num_threads);
    std::vector<std::atomic_char> seen(num_elements);

    for (unsigned int x = 0; x < num_threads; ++x) {
        for (unsigned int i = 0; i < num_elements; ++i) {
            data[x * num_elements + i] = new TestObjA(0, i);
        }
    }

    std::clog << "Using " << num_threads << " threads" << std::endl;
    std::atomic_bool success = true;
    {
        std::vector<std::jthread> threads;
        threads.reserve(num_threads);
        for (auto i = 0u; i < num_threads; ++i) {
            threads.emplace_back([&, i] {
                std::size_t j = next_value.fetch_add(1);
                while (j < num_elements) {
                    ops_[i].store(data[i * num_elements + j]);
                    add_ops_to_root(queue, num_threads, i);

                    j = next_value.fetch_add(1);
                }
            });
        }
    }

    // queue.print_all();

    TestObjA* a = nullptr;
    while ((a = queue.peek(0)) != nullptr) {
        seen[a->value].fetch_add(1);
        queue.pop_if(a->timestamp, 0);
    }
    int missing = 0;
    for (int i = 1; i < num_elements; ++i) {
        if (seen[i] == 0) {
            ++missing;
            std::clog << i << " missing" << std::endl;
            success = false;
        }
    } 

    for (unsigned int x = 0; x < num_threads; ++x) {
        for (unsigned int i = 0; i < num_elements; ++i) {
            delete data[x * num_elements + i];
        }
    }

    if (!success) {
        std::clog << "Root input test failed" << std::endl;
        std::clog << missing << " values missing" << std::endl;
        return false;
    }

    std::clog << "Root input test successfull\n";
    return true;
}

int main() {
    return !root_input_test() || !input_test() || !removal_test();
}
