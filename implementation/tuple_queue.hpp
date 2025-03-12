#pragma once

#include "implementation/conditional_hazard_pointers.hpp"

#include <memory>
#include <iostream>
#include <cstdint>
#include <limits>

#include <boost/atomic/atomic.hpp>

/**
 * Adaptation of the WaitFree Queue that saves two values.
 * I made this because a struct that contains a shared_ptr is not trivialy copyable but that is neccessary for atomics
 */
template <class T1, class T2>
class TupleQueue {
private:
  struct Node {
    boost::atomic<Node *> next;
    std::size_t push_tid;
    boost::atomic<std::size_t> pop_tid;
    boost::atomic<T1> value;
    boost::atomic<T2> value2;
  };

  enum OpType {
    kPush = 0,
    kPop = 2,
    kNotPending = 3,
  };

  struct alignas(128) OpDesc {
    // these should be const but then the atomics won't work
    Node* node;
    std::uint64_t timestamp_type = 0;

    OpDesc() : node(nullptr), timestamp_type((static_cast<std::uint64_t>(OpType::kNotPending)<<(std::numeric_limits<std::uint64_t>::digits-2))) {}

    OpDesc(Node* init_node, std::uint64_t timestamp, OpType type) : node(init_node), timestamp_type(((~(static_cast<std::uint64_t>(0b11)<<(std::numeric_limits<std::uint64_t>::digits-2))) & timestamp) + (static_cast<std::uint64_t>(type)<<(std::numeric_limits<std::uint64_t>::digits-2))) {}

    std::uint64_t get_timestamp() {
      return (~(static_cast<std::uint64_t>(0b11)<<(std::numeric_limits<std::uint64_t>::digits-2))) & timestamp_type;
    }

    OpType get_type() {
      return static_cast<OpType>(timestamp_type>>(std::numeric_limits<std::uint64_t>::digits-2));
    }
  };

  using pNode = Node *;

  const std::size_t max_threads_  = 1;

  boost::atomic<pNode> head;
  boost::atomic<pNode> tail;

  ConditionalHazardPointers<Node, T1> hp;

  static constexpr int kHpTail = 0;
  static constexpr int kHpHead = 1;
  static constexpr int kHpNext = 2;

  std::vector<boost::atomic<OpDesc>> opdescs_;
  boost::atomic<std::uint64_t> next_timestamp_ = 1;

  bool isStillPending(const std::size_t i, const std::uint64_t timestamp) const {
    OpDesc d = opdescs_[i].load();
    return d.get_type() != OpType::kNotPending && d.get_timestamp() <= timestamp;
  }

  void help(std::uint64_t timestamp, std::size_t tid) {
    for (std::size_t i = 0; i < max_threads_; ++i) {
      OpDesc d = opdescs_[i].load();
      if (d.get_type() != OpType::kNotPending && d.get_timestamp() <= timestamp) {
        if (d.get_type() == OpType::kPush) {
          help_push(i, timestamp, tid);
        } else if (d.get_type() == OpType::kPop) {
          help_pop(i, timestamp, tid);
        }
      }
    }
  }

  void help_push(std::size_t i, std::uint64_t timestamp, std::size_t tid) {
    while (isStillPending(i, timestamp)) {
      pNode curr_tail = hp.protectPtr(kHpTail, tail.load(), tid);
      if (curr_tail != tail.load()) continue;
      pNode curr_next = hp.protectPtr(kHpNext, curr_tail->next.load(), tid);
      if (curr_tail == tail.load()) {
        if (curr_next == nullptr) {
          // std::cout << "f" << std::endl;
          if (isStillPending(i, timestamp)) {
            // std::cout << "b" << std::endl;
            if (curr_tail->next.compare_exchange_strong(curr_next, opdescs_[i].load().node)) {
              // std::cout << "e" << std::endl;
              hp.clearOne(kHpTail, tid);
              hp.clearOne(kHpNext, tid);
              help_finish_push(tid);
              return;
            }
          }
        } else {
          hp.clearOne(kHpTail, tid);
          hp.clearOne(kHpNext, tid);
          help_finish_push(tid);
        }
      }
    }
  }

  void help_finish_push(std::size_t tid) {
    pNode curr_tail = hp.protect(kHpTail, tail.load(), tid);
    if (curr_tail != tail.load()) return;
    pNode curr_next = hp.protectPtr(kHpNext, curr_tail->next.load(), tid);
    if (curr_tail == tail.load() && curr_next != nullptr) {
      std::size_t i = curr_next->push_tid;
      OpDesc d = opdescs_[i].load();
      if (curr_tail == tail.load() && d.node == curr_next) {
        OpDesc new_d(d.node, d.get_timestamp(), OpType::kNotPending);
        opdescs_[i].compare_exchange_strong(d, new_d);
        tail.compare_exchange_strong(curr_tail, curr_next);
      }
    }
    hp.clearOne(kHpTail, tid);
    hp.clearOne(kHpNext, tid);
  }

  void help_pop(std::size_t i, std::uint64_t timestamp, std::size_t tid) {
    while (isStillPending(i, timestamp)) {
      pNode curr_head = hp.protectPtr(kHpHead, head.load(), tid);
      if (curr_head != head.load()) continue;
      pNode curr_tail = hp.protectPtr(kHpTail, tail.load(), tid);
      if (curr_tail != tail.load()) continue;      
      pNode curr_next = hp.protectPtr(kHpNext, curr_head->next.load(), tid);

      if (curr_head == head.load()) {
        if (curr_head == curr_tail) {
          if (curr_next == nullptr) {
            OpDesc d = opdescs_[i].load();
            if (curr_tail == tail.load() && isStillPending(i, timestamp)) {
              OpDesc new_d(nullptr, d.get_timestamp(), OpType::kNotPending);
              opdescs_[i].compare_exchange_strong(d, new_d);
              hp.clearOne(kHpNext, tid);
              hp.clearOne(kHpHead, tid);
              hp.clearOne(kHpTail, tid);
            }
          } else {
            hp.clearOne(kHpHead, tid); //tail and next get cleared in finish_push fct
            help_finish_push(tid);
          }
        } else {
          OpDesc d = opdescs_[i].load();
          pNode n = d.node;
          if (!isStillPending(i, timestamp)) {
            hp.clearOne(kHpNext, tid);
            hp.clearOne(kHpHead, tid);
            hp.clearOne(kHpTail, tid);
            break;
          }
          if (curr_head == head.load() && n != curr_head) {
            OpDesc new_d(curr_head, d.get_timestamp(), d.get_type());
            if (!opdescs_[i].compare_exchange_strong(d, new_d))
              continue;
          }
          std::size_t cp_max_threads = max_threads_;
          curr_head->pop_tid.compare_exchange_strong(cp_max_threads, i);
          hp.clearOne(kHpTail, tid);
          help_finish_pop(tid);
        }
      }
    }
  }

  void help_finish_pop(std::size_t tid) {
    pNode curr_head = hp.protectPtr(kHpHead, head.load(), tid);
    if (curr_head != head.load()) return;
    pNode curr_next = hp.protectPtr(kHpNext, curr_head->next.load(), tid);
    std::size_t i = curr_head->pop_tid.load();
    if (i != max_threads_) {
      OpDesc d = opdescs_[i].load();
      if (curr_head == head.load() && curr_next != nullptr) {
        OpDesc new_d(d.node, d.get_timestamp(), OpType::kNotPending);
        opdescs_[i].compare_exchange_strong(d, new_d);
        if (head.compare_exchange_strong(curr_head, curr_next)) {
          // hp.retire(curr_head, tid);
        }
      }
    }
    hp.clearOne(kHpHead, tid);
    hp.clearOne(kHpNext, tid);
  }

public:
  TupleQueue(std::size_t max_threads) : max_threads_(max_threads), hp(3, max_threads), opdescs_(max_threads) {
    pNode n = new Node;
    n->next = nullptr;
    n->push_tid = 0;
    n->pop_tid = max_threads_;
    head.store(n);
    tail.store(n);

    for (std::size_t i = 0; i < max_threads_; ++i) {
      opdescs_[i].store({nullptr, 0, OpType::kNotPending});
    }
  }

  ~TupleQueue() {
    pNode n = head.load();
    do {
      pNode tmp = n->next.load();
      delete n;
      n = tmp;
    } while (n != nullptr);
  }

  void push(T1 value1, T2 value2, std::size_t tid) {
    pNode n = new Node;
    n->next = nullptr;
    n->push_tid = tid;
    n->value = value1;
    n->value2 = value2;
    n->pop_tid = max_threads_;

    std::uint64_t timestamp = next_timestamp_.fetch_add(1);
    OpDesc d(n, timestamp, OpType::kPush);
    opdescs_[tid].store(d);
    help(timestamp, tid);
    help_finish_push(tid);
  }

  std::pair<T1,T2> pop(std::size_t tid) {
    std::uint64_t timestamp = next_timestamp_.fetch_add(1);
    OpDesc d(nullptr, timestamp, OpType::kPop);
    opdescs_[tid].store(d);
    help(timestamp, tid);
    help_finish_pop(tid);
    d = opdescs_[tid].load();

    if (d.node == nullptr)  
      return std::pair<T1,T2>{};
    pNode next = d.node->next.load();
    std::pair<T1,T2> return_value = {next->value,  next->value2};
    d.node->next.load()->value.store(T1{});
    d.node->next.store(nullptr);
    hp.retire(d.node, tid);
    return return_value;
  }

  void print_atomic_capabilities() {
    boost::atomic<OpDesc> a = OpDesc(nullptr, 0, OpType::kPush);
    std::cout << "TupleQ op: " << a.is_lock_free() << std::endl;
    std::cout << "TupleQ opdesc size: " << sizeof(OpDesc) << std::endl;
  }
};