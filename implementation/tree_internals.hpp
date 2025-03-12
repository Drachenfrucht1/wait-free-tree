#pragma once

#include "waitfree_queue.hpp"
#include "tuple_queue.hpp"
#include "conditional_q.hpp"

#include <cstdint>
#include <limits>
#include <unordered_map>

#include <boost/atomic/atomic.hpp>

template <class T>
struct Node;

enum OperationType {
  kInsert,
  kRemove,
  kLookup,
  kRangeCount,
};

template <class T>
struct Operation {
  const OperationType type;
  boost::atomic<std::uint64_t> timestamp = 0;
  TupleQueue<Node<T>*, std::uint32_t> to_visit;
  const T value = T{};
  const T value2 = T{};
  boost::atomic<T> split = T{};
  boost::atomic<std::uint32_t> lower_count = 0;
  boost::atomic<std::uint32_t> upper_count = 0;
  boost::atomic<bool> success = false;

  Operation(std::size_t max_threads, OperationType init_type, const T init_value, const T init_value2 = T{}) :  type(init_type), to_visit(max_threads), value(init_value), value2(init_value2) {}
};

struct NodeState {
  std::uint64_t timestamp_active;
  // const std::uint64_t last_timestamp = 0;
  std::uint32_t all_children = 0;
  std::uint32_t changes = 0;
  // const bool active = true;

  NodeState(std::uint64_t last_timestamp, std::uint32_t new_children, std::uint32_t new_changes, bool active = true) : timestamp_active(((~(static_cast<std::uint64_t>(1)<<(std::numeric_limits<std::uint64_t>::digits-1))) & last_timestamp) + (static_cast<std::uint64_t>(active) * (static_cast<std::uint64_t>(1)<<(std::numeric_limits<std::uint64_t>::digits-1)))), all_children(new_children), changes(new_changes) {}

  bool get_active() {
    return timestamp_active>>(std::numeric_limits<std::uint64_t>::digits-1);
  }

  std::uint64_t get_last_timestamp() {
    return (~(static_cast<std::uint64_t>(1)<<(std::numeric_limits<std::uint64_t>::digits-1))) & timestamp_active;
  }
};

template <class T>
struct Node {
  boost::atomic<NodeState> state;
  ConditionalQ<Operation<T>> ops;
  const std::uint64_t init_size;
  const T value;
  boost::atomic<Node<T> *> left_child = nullptr;
  boost::atomic<Node<T> *> right_child = nullptr;

  Node(std::size_t max_threads, const std::uint64_t init_init_size, const T init_value, NodeState initial_state) : state(initial_state), ops(max_threads), init_size(init_init_size), value(init_value) {}
  ~Node() {}
};

template <class T>
struct NodeRemoveFlags {
  std::uint64_t remove_flags = 0;
  Node<T>* node;

  friend bool operator==(const NodeRemoveFlags<T>& lhs, const NodeRemoveFlags<T>& rhs) {
    return lhs.node == rhs.node && lhs.remove_flags == rhs.remove_flags;
  }
};