#pragma once

#include "implementation/hazard_pointers.hpp"

#include <memory>
#include <iostream>
#include <cstdint>
#include <limits>

#include <boost/atomic/atomic.hpp>

/**
 * Adaptation of the WaitFree Queue that only allows values to be inserted in a specific order.
 * T has to have a member called timestamp.
 */
template <class T>
class ConditionalQ {
private:
  struct Node {
    boost::atomic<Node *> next;
    std::size_t push_tid;
    boost::atomic<std::size_t> pop_tid;
    T* value;
    //without being atomic this causes data races, not sure why
    boost::atomic<std::uint64_t> timestamp; //same as value->timestamp
  };

  enum OpType {
    kPush,
    kPop,
    kPeek,
    kNotPending,
  };

  struct OpDesc {
    union {
      std::uint64_t timestamp_external;
      Node* node;
      T* value;
    };
    std::uint64_t timestamp_type = 0;

    OpDesc() : node(nullptr), timestamp_type((static_cast<std::uint64_t>(OpType::kNotPending)<<(std::numeric_limits<std::uint64_t>::digits-2))) {}

    OpDesc(std::uint64_t timestamp, OpType type) : timestamp_type(((~(static_cast<std::uint64_t>(0b11)<<(std::numeric_limits<std::uint64_t>::digits-2))) & timestamp) + (static_cast<std::uint64_t>(type)<<(std::numeric_limits<std::uint64_t>::digits-2))) {}

    std::uint64_t get_timestamp() {
      return (~(static_cast<std::uint64_t>(0b11)<<(std::numeric_limits<std::uint64_t>::digits-2))) & timestamp_type;
    }

    OpType get_type() {
      return static_cast<OpType>(timestamp_type>>(std::numeric_limits<std::uint64_t>::digits-2));
    }

    static OpDesc create_with_node(Node* n, std::uint64_t timestamp, OpType type) {
      OpDesc d(timestamp, type);
      d.node = n;
      return d;
    }
    static OpDesc create_with_timestamp(std::uint64_t timestamp_ext, std::uint64_t timestamp, OpType type) {
      OpDesc d(timestamp, type);
      d.timestamp_external = timestamp_ext;
      return d;
    }
    static OpDesc create_with_value(T* value, std::uint64_t timestamp, OpType type) {
      OpDesc d(timestamp, type);
      d.value = value;
      return d;
    }

  };

  using pNode = Node *;

  const std::size_t max_threads_  = 1;

  boost::atomic<pNode> head;
  boost::atomic<pNode> tail;

  HazardPointers<Node> hp;

  static constexpr int kHpTail = 0;
  static constexpr int kHpHead = 1;
  static constexpr int kHpNext = 2;
  static constexpr int kHpInsertNode = 1;

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
        } else {
          help_peek(i, timestamp, tid);
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
        //special code to only insert elements in increasing order
        OpDesc d = opdescs_[i].load();
        if (d.node == nullptr) {
          hp.clearOne(kHpTail, tid);
          return;
        }
        hp.protectPtr(kHpInsertNode, d.node, tid);
        if (opdescs_[i].load().node != d.node) {
          hp.clearOne(kHpTail, tid);
          hp.clearOne(kHpInsertNode, tid);
          return;
        }

        if (isStillPending(i, timestamp)) {
          std::uint64_t tail_ts = curr_tail->timestamp;
          std::uint64_t node_ts = d.node->timestamp;
          if (tail_ts >= node_ts) {
            OpDesc new_d = OpDesc::create_with_node(nullptr, d.get_timestamp(), OpType::kNotPending);
            if (opdescs_[i].compare_exchange_strong(d, new_d))
              hp.retire(d.node, tid);
            hp.clearOne(kHpTail, tid);
            hp.clearOne(kHpNext, tid);
            hp.clearOne(kHpInsertNode, tid);
            return;
          }
        }

        if (curr_next == nullptr) {
          if (isStillPending(i, timestamp)) {
            if (curr_tail->next.compare_exchange_strong(curr_next, opdescs_[i].load().node)) {
              hp.clearOne(kHpTail, tid);
              hp.clearOne(kHpNext, tid);
              hp.clearOne(kHpInsertNode, tid);
              help_finish_push(tid);
              return;
            }
          }
        } else {
          hp.clearOne(kHpTail, tid);
          hp.clearOne(kHpNext, tid);
          hp.clearOne(kHpInsertNode, tid);
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
      // std::cout << "c" << std::endl;
      std::size_t i = curr_next->push_tid;
      OpDesc d = opdescs_[i].load();
      if (curr_tail == tail.load() && d.node == curr_next) {
        // std::cout << "d" << std::endl;
        OpDesc new_d = OpDesc::create_with_node(d.node, d.get_timestamp(), OpType::kNotPending);
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
              OpDesc new_d = OpDesc::create_with_timestamp(0, d.get_timestamp(), OpType::kNotPending);
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
          // pNode n = d.node;
          if (!isStillPending(i, timestamp)) {
            hp.clearOne(kHpNext, tid);
            hp.clearOne(kHpHead, tid);
            hp.clearOne(kHpTail, tid);
            break;
          }

          //this should only be entered once. Either the timestamp matches -> go to help_finish_pop 
          // or it doesnt -> cancel op
          if (curr_head == head.load() /*&& n != curr_head*/) {
            if (curr_next->timestamp != d.timestamp_external) {
              OpDesc new_d = OpDesc::create_with_timestamp(0, d.get_timestamp(), OpType::kNotPending);
              opdescs_[i].compare_exchange_strong(d, new_d);
              hp.clearOne(kHpTail, tid);
              hp.clearOne(kHpHead, tid);
              hp.clearOne(kHpNext, tid);
              return;
            }

            //to compensate for missing continue after cas
            OpDesc d2 = opdescs_[i].load();
            if (d.timestamp_external != d2.timestamp_external || d.timestamp_type != d2.timestamp_type)
              continue;

            // OpDesc new_d(d);
            // new_d.node = curr_head;
            // new_d.value = curr_next->value;
            // if (!opdescs_[i].compare_exchange_strong(d, new_d))
            //   continue;
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
        OpDesc new_d = OpDesc::create_with_timestamp(d.timestamp_external, d.get_timestamp(), OpType::kNotPending);
        opdescs_[i].compare_exchange_strong(d, new_d);
        if (head.compare_exchange_strong(curr_head, curr_next)) {
          hp.retire(curr_head, tid);
        }
      }
    }
    hp.clearOne(kHpHead, tid);
    hp.clearOne(kHpNext, tid);
  }

  void help_peek(std::size_t i, std::uint64_t timestamp, std::size_t tid) {
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
              OpDesc new_d = OpDesc::create_with_value(nullptr, d.get_timestamp(), OpType::kNotPending);
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
          if (!isStillPending(i, timestamp)) {
            hp.clearOne(kHpNext, tid);
            hp.clearOne(kHpHead, tid);
            hp.clearOne(kHpTail, tid);
            break;
          }
          if (curr_head == head.load()) {
            OpDesc new_d = OpDesc::create_with_value(curr_next->value, d.get_timestamp(), OpType::kNotPending);
            if (!opdescs_[i].compare_exchange_strong(d, new_d))
              continue;
          }
          hp.clearOne(kHpNext, tid);
          hp.clearOne(kHpHead, tid);
          hp.clearOne(kHpTail, tid);
        }
      }
    }
  }

public:
  ConditionalQ(std::size_t max_threads) : max_threads_(max_threads), hp(3, max_threads), opdescs_(max_threads) {
    pNode n = new Node;
    n->next = nullptr;
    n->push_tid = 0;
    n->pop_tid = max_threads_;
    n->value = nullptr;
    n->timestamp = std::uint64_t{};
    head.store(n);
    tail.store(n);

    // std::cout << "Lockfree OpDesc: " << opdescs_[0].is_lock_free() << std::endl;
    // std::cout << "OpDesc siz: " << sizeof(OpDesc) << std::endl;
  }

  ~ConditionalQ() {
    pNode n = head.load();
    do {
      pNode tmp = n->next.load();
      delete n;
      n = tmp;
    } while (n != nullptr);
  }

  /**
   * Returns the value at the front of the queue
   */
  [[nodiscard]] T* peek(std::size_t tid) {
    std::uint64_t timestamp = next_timestamp_.fetch_add(1);
    OpDesc d = OpDesc::create_with_value(nullptr, timestamp, OpType::kPeek);
    opdescs_[tid].store(d);
    help(timestamp, tid);
    help_finish_pop(tid);
    d = opdescs_[tid].load();

    return d.value;
  }

  /**
   * Adds value to the queue iff the current tail of the queue has a smaller timestamp
   */
  void push_if(T* value, std::size_t tid) {
    pNode n = new Node;
    n->next = nullptr;
    n->push_tid = tid;
    n->value = value;
    n->pop_tid = max_threads_;
    n->timestamp.store(value->timestamp);

    std::uint64_t timestamp = next_timestamp_.fetch_add(1);
    OpDesc d = OpDesc::create_with_node(n, timestamp, OpType::kPush);
    opdescs_[tid].store(d);
    help(timestamp, tid);
    help_finish_push(tid);
  }

  /**
   * Removes the first value from the queue if its timestamp is timestamp_a
   * Does not return the removed value
   */
  void pop_if(std::uint64_t timestamp_a, std::size_t tid) {
    std::uint64_t timestamp = next_timestamp_.fetch_add(1);
    OpDesc d = OpDesc::create_with_timestamp(timestamp_a, timestamp, OpType::kPop);
    opdescs_[tid].store(d);
    help(timestamp, tid);
    help_finish_pop(tid);
  }

  void print_all() {
    pNode n = head.load()->next.load();
    while (n != nullptr) {
      std::cout << n->value->timestamp << std::endl;
      n = n->next.load();
    }
  }

  void print_atomic_capabilities() {
    boost::atomic<OpDesc> a;
    std::cout << "ConditionalQ op: " << a.is_lock_free() << std::endl;
    std::cout << "ConditionalQ op size: " << sizeof(OpDesc) << std::endl;
  }
};