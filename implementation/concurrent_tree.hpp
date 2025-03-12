#pragma once

#include "conditional_q.hpp"
#include "waitfree_queue.hpp"
#include "tree_internals.hpp"

#include "hazard_pointers.hpp"

#include <vector>
#include <cstdint>
#include <memory>
#include <algorithm>
#include <queue>
#include <iostream>
#include <limits>

#include <boost/atomic/atomic.hpp>


/**
 * Implementation of the Wait-free Trees with Asymptotically-Efficient Range Queries proposed by Kokorin, Yudov, Aksenov, and Alistarh
 * The wait-freeness is somewhat destroyed by 128bit atomics not working with gcc and the tree node deallocation scheme, which is not bounded.
 */
template <class T, bool rebuild_b = true>
class ConcurrentTree {
public:

  /**
   * Creates an empty tree that allows concurrent access by max_threads threads
   */
  ConcurrentTree(std::size_t max_threads) : max_threads_(max_threads), fake_root_q(max_threads_), ops_(max_threads_), delete_mask_((static_cast<std::uint64_t>(1)<<max_threads_)-1), to_be_deleted_(max_threads_), hp_op(max_threads_, max_threads_)  {
    for (std::size_t i = 0; i < max_threads_; ++i) {
      ops_[i].store(nullptr);
    }
  }

  /**
   * Creates a tree that allows concurrent access by max_threads threads
   * The tree will contain the values in the initial_values vector
   */
  ConcurrentTree(std::vector<T> initial_values, std::size_t max_threads) : max_threads_(max_threads), fake_root_q(max_threads_), ops_(max_threads_), delete_mask_((static_cast<std::uint64_t>(1)<<max_threads_)-1), to_be_deleted_(max_threads_), hp_op(max_threads_, max_threads_)  {
    for (std::size_t i = 0; i < max_threads_; ++i) {
      ops_[i].store(nullptr);
    }
    std::sort(initial_values.begin(), initial_values.end());
    fake_root_child.store(build_tree(initial_values, 0, initial_values.size() - 1, 1));
  }
  
  ~ConcurrentTree() {
    //delete the remaining nodes of the tree
    std::queue<pNode> q;
    q.push(fake_root_child.load());
    while (!q.empty()) {
      auto n = q.front();
      q.pop();
      if (n->left_child.load() != nullptr)
        q.push(n->left_child.load());
      if (n->right_child.load() != nullptr)
        q.push(n->right_child.load());
      std::atomic_thread_fence(std::memory_order_seq_cst);
      delete n;
    }
    
    //delete the remaining nodes that are marked to be deleted
    auto p1 = to_be_deleted_.pop(0);
    while (p1.node != nullptr) {
      delete_tree(p1.node);
      p1 = to_be_deleted_.pop(0);
    }
  }

  /*
   * Inserts a value into the tree
   * It must hold that value != T{}
   * Inserting a value that is already part of the tree can lead to wrong results for the range queries, as the values along the path are still updated
   */
  bool insert(const T value, const std::size_t tid) {
    set_mask_.fetch_and(~(static_cast<std::uint64_t>(1)<<tid));

    //T{} is used as sentinel, so cant be a valid value to insert
    if (value == T{})
      return false;

    pOp new_op = new Op(max_threads_, OperationType::kInsert, value);
    ops_[tid].store(new_op);
    add_ops_to_root(tid);

    do_op(tid);

    ops_[tid].store(nullptr);
    bool result = new_op->success;
    hp_op.retire(new_op, tid);

    return result;
  }

  /**
   * Remove a value from the tree
   * Removing a value that is not part of the tree can lead to wrong results for the range queries, as the values along the path are still updated
   */
  void remove(const T value, const std::size_t tid) {
    set_mask_.fetch_and(~(static_cast<std::uint64_t>(1)<<tid));

    pOp new_op = new Op(max_threads_, OperationType::kRemove, value);
    ops_[tid].store(new_op);
    add_ops_to_root(tid);

    do_op(tid);

    ops_[tid].store(nullptr);
    hp_op.retire(new_op, tid);
  }

  /**
   * Returns true if value is part of the tree, false if it is not
   */
  [[nodiscard]] bool lookup(const T value, const std::size_t tid) {
    set_mask_.fetch_and(~(static_cast<std::uint64_t>(1)<<tid));

    pOp new_op = new Op(max_threads_, OperationType::kLookup, value);
    ops_[tid].store(new_op);
    add_ops_to_root(tid);

    do_op(tid);
    ops_[tid].store(nullptr);

    bool result = new_op->success;

    hp_op.retire(new_op, tid);

    return result;
  }

  /**
   * Returns the number of elements of the closed interval [lower, upper] that are part of the tree
   */
  [[nodiscard]] std::uint32_t range_count(const T lower, const T upper, const std::size_t tid) {
    if (lower == upper) {
      return lookup(lower, tid);
    }

    set_mask_.fetch_and(~(static_cast<std::uint64_t>(1)<<tid));

    pOp new_op = new Op(max_threads_, OperationType::kRangeCount, lower, upper);
    ops_[tid].store(new_op);
    add_ops_to_root(tid);

    std::uint32_t result = do_op(tid);
    ops_[tid].store(nullptr);

    hp_op.retire(new_op, tid);

    return result;
  }

  void print_atomic_capabilities() {
    to_be_deleted_.print_atomic_capabilities();
    fake_root_q.print_atomic_capabilities();
    boost::atomic<NodeState> a = NodeState(0, 0, 0);
    std::cout << "NodeState: " << a.is_lock_free() << std::endl;
    std::cout << "Nodestate size: " << sizeof(NodeState) << std::endl;
    std::cout << "Node<T> size: " << sizeof(Node<T>) << std::endl;
  }

private:
  using Op = Operation<T>;
  using pOp = Op *;
  using pState = NodeState *;
  using pNode = Node<T> *;

  std::size_t max_threads_ = 1;

  boost::atomic<pNode> fake_root_child = nullptr;
  ConditionalQ<Op> fake_root_q;

  std::vector<boost::atomic<pOp>> ops_;

  boost::atomic<std::uint64_t> last_timestamp_ = 1;

  const std::uint64_t delete_mask_;
  boost::atomic<std::uint64_t> set_mask_ = 0;
  WaitFreeQueue<NodeRemoveFlags<T>> to_be_deleted_;
  boost::atomic<std::uint64_t> to_be_deleted_num_ = 0;

  HazardPointers<Op> hp_op;

  /**
   * Insert the operation of thread tid into the root queue
   * While doing so, assign the operation a timestamp and try to insert all operations with a lower timestamp into the root queue
   * This is to maintain the ordering of the operations
   */
  void add_ops_to_root(std::size_t tid) {
    std::vector<pOp> to_insert;
    std::uint64_t own_timestamp = 0;
    std::uint64_t new_timestamp = last_timestamp_.fetch_add(1);
    if (ops_[tid].load()->timestamp.compare_exchange_strong(own_timestamp, new_timestamp)) { //this op can only be freed by this thread -> no hp
      own_timestamp = new_timestamp;
    }
    to_insert.push_back(ops_[tid].load());
    for (std::size_t i = 0; i < max_threads_; ++i) {
      pOp a = hp_op.protectPtr(i, ops_[i].load(), tid);
      if (a == nullptr)
        continue;
      if (a == ops_[i].load()) {
        std::size_t check_timestamp = 0;
        new_timestamp = last_timestamp_.fetch_add(1);
        if(!a->timestamp.compare_exchange_strong(check_timestamp, new_timestamp)) {
          if (check_timestamp < own_timestamp) {
            to_insert.push_back(a);
          }
        }
      }
    }
    // try to push in sorted order to maintain ordering of operations 
    std::sort(to_insert.begin(), to_insert.end(), [](pOp a, pOp b) {return a->timestamp < b->timestamp;});
    for (auto a : to_insert) {
      fake_root_q.push_if(a, tid);
    }

    hp_op.clear(tid);
  }

  /**
   * Complete the operation of tid by executing the action in all nodes that the operation has to visit
   */
  std::uint32_t do_op(const std::size_t tid) { 
    std::unordered_map<pNode, std::uint32_t> results;
    pOp own_op = ops_[tid].load();
    //do in root q
    execute_until_timestamp_root(own_op->timestamp, tid);
    
    //do in other q's
    std::pair<pNode, std::uint32_t> n_r = std::pair<pNode, std::uint32_t>{};
    while ((n_r = own_op->to_visit.pop(tid)) != std::pair<pNode, std::uint32_t>{}) {
      if (!results.contains(n_r.first)) { 
        results.insert({n_r.first, n_r.second});
      }
      execute_until_timestamp(n_r.first, own_op->timestamp, tid);
    }
    // if (!own_op->success) {
    //   std::cout << "a " << own_op->value << " " << tid << '\n';
    //   for (auto p : results) {
    //     std::cout << "b " << p.first->value << '\n';
    //     if (p.first->left_child.load() != nullptr) {
    //       std::cout << p.first->left_child.load()->value << '\n';
    //     }
    //     if (p.first->right_child.load() != nullptr) {
    //       std::cout << p.first->right_child.load()->value << '\n';
    //     }
    //   }
    // }

    //collect results, this is only relevant for the range count query
    std::uint32_t result = 0;
    for (auto p : results) {
      result += p.second;
    }
    result += own_op->lower_count.load() + own_op->upper_count.load();

    //iterate through nodes that are marked to be deleted, now that this thread access no nodes anymore
    set_mask_.fetch_or(1<<tid);

    //this kills the wait-freenes, a const amount of iterations that keep the size of to_be_deleted_ small would better
    std::uint64_t max_delete = to_be_deleted_num_.load();
    for (std::uint64_t i = 0; i < max_delete; ++i) {
      auto p = to_be_deleted_.pop(tid);
      if (p.node == nullptr) {
        continue;
      }
      p.remove_flags |= set_mask_.load();
      if (p.remove_flags == delete_mask_) {
        delete_tree(p.node);
        to_be_deleted_num_.fetch_sub(1);
      } else {
        to_be_deleted_.push(p, tid);
      }
    }
    return result;
  }

  /**
   * Execute actions in the (fake) root node until the given timestamp is reached
   */
  void execute_until_timestamp_root(const std::uint64_t timestamp, const std::size_t tid) {
    pOp a;
    while (true) {
      a = hp_op.protectPtr(0, fake_root_q.peek(tid), tid);
      if (a != fake_root_q.peek(tid)) 
        continue;

      if (a == nullptr) break;
      if (a->timestamp > timestamp) break;

      if (rebuild_b) {
        if (!rebuild_root(a->timestamp, tid))
          continue;
      }

      if (a->type == OperationType::kInsert) {
        do_root_insert(a, tid);
      } else if (a->type == OperationType::kRemove) {
        do_root_remove(a, tid);
      } else if (a->type == OperationType::kLookup) {
        do_root_lookup(a, tid);
      } else if (a->type == OperationType::kRangeCount) {
        do_root_rangecount(a, tid);
      }

      hp_op.clearOne(0, tid);
    }
  }

  /**
   * Execute actions in n until the given timestamp is reached
   */
  void execute_until_timestamp(const pNode n, const std::uint64_t timestamp, const std::size_t tid, const std::size_t index = 0) {
    pOp a;
    while (true) {
      a = hp_op.protectPtr(index, n->ops.peek(tid), tid);
      if (a != n->ops.peek(tid)) 
        continue;

      if (a == nullptr) break;
      if (a->timestamp > timestamp) break;

      if (rebuild_b) {
        if (!rebuild_node(n, a->timestamp, tid))
          continue;
      }

      if (a->type == OperationType::kInsert) {
        do_node_insert(a, n, tid);
      } else if (a->type == OperationType::kRemove) {
        do_node_remove(a, n, tid);
      } else if (a->type == OperationType::kLookup) {
        do_node_lookup(a, n, tid);
      } else if (a->type == OperationType::kRangeCount) {
        do_node_rangecount(a, n, tid);
      }

      hp_op.clearOne(index, tid);
    }
  }

  /**
   * Execute an insert action in the (fake) root
   * op needs to be protected by hp
   */
  void do_root_insert(const pOp op, const std::size_t tid) {
    pNode child = fake_root_child.load();
    if (child == nullptr) {
      // std::cout << "a " << op->value << " r " << tid << "\n"; 
      NodeState new_state(op->timestamp, 1, 0);
      pNode new_node = new Node<T>(max_threads_, 1, op->value, new_state);
      if (!fake_root_child.compare_exchange_strong(child, new_node)) {
        delete new_node;
      } else {
        op->success.store(true);
      }
    } else {
      // std::cout << "b " << op->value << " " << child->value << " " << tid << "\n"; 
      NodeState curr_state = child->state.load();

      if (child->value == op->value) {
        //node with desired value exists
        //ignore when the insert operation has already passed this node
        //reactive if it is a deactivated node
        if (curr_state.get_last_timestamp() >= op->timestamp) {
          fake_root_q.pop_if(op->timestamp, tid);
          return;
        }

        if (curr_state.get_active()) {
          op->split.store(op->value);            
        } else {
          NodeState new_state(op->timestamp, curr_state.all_children, curr_state.changes, true);

          if (child->state.compare_exchange_strong(curr_state, new_state)) {
            op->success.store(true);
          }
        }
      } else {
        //node value does not match
        //push operation to child
        op->to_visit.push(child, 0, tid);
        if (curr_state.get_last_timestamp() < op->timestamp) {
          NodeState new_state(op->timestamp, curr_state.all_children+1, curr_state.changes+1, curr_state.get_active());
          
          child->state.compare_exchange_strong(curr_state, new_state);
        }
        child->ops.push_if(op, tid);
      }
    }

    fake_root_q.pop_if(op->timestamp, tid);
  }

  /**
   * Execute a lookup action in the (fake) root
   * op needs to be protected by hp
   */
  void do_root_lookup(const pOp op, const std::size_t tid) {
    pNode child = fake_root_child.load();
    if (child != nullptr) {
      NodeState curr_state = child->state.load();

      if (child->value == op->value) {
        if (curr_state.get_active() && curr_state.get_last_timestamp() < op->timestamp)
          op->success.store(true);
      } else {
        op->to_visit.push(child, 0, tid);
      }
      
      if (curr_state.get_last_timestamp() < op->timestamp) {
        NodeState new_state(op->timestamp, curr_state.all_children, curr_state.changes, curr_state.get_active());
        
        child->state.compare_exchange_strong(curr_state, new_state);
      }

      if (child->value != op->value)
        child->ops.push_if(op, tid);
    }
    fake_root_q.pop_if(op->timestamp, tid);
  }

  /**
   * Execute a remove action in the (fake) root
   * Nodes are only marked as inactive and will be removed when the subtree is rebuild
   * op needs to be protected by hp
   */
  void do_root_remove(const pOp op, const std::size_t tid) {
    pNode child = fake_root_child.load();

    if (child != nullptr) {
      NodeState curr_state = child->state.load();

      if (child->value != op->value) 
        op->to_visit.push(child, 0, tid);

      if (curr_state.get_last_timestamp() < op->timestamp) {
        NodeState new_state(op->timestamp, curr_state.all_children-1, curr_state.changes+1, curr_state.get_active() && child->value != op->value);
        
        child->state.compare_exchange_strong(curr_state, new_state);
      }

      if (child->value != op->value) 
        child->ops.push_if(op, tid);
    }

    fake_root_q.pop_if(op->timestamp, tid);
  }

  /**
   * Execute a range_count action in the (fake) root
   * op needs to be protected by hp
   */
  void do_root_rangecount(const pOp op, const std::size_t tid) {
    pNode child = fake_root_child.load();
    if (child != nullptr) {
        if (child->value >= op->value && child->value <= op->value2) {
          T cas_standin = T{};
          op->split.compare_exchange_strong(cas_standin, child->value);
          op->to_visit.push(child, 1, tid);
          child->ops.push_if(op, tid);
        }
        op->to_visit.push(child, 0, tid);
        child->ops.push_if(op, tid);
      }
    fake_root_q.pop_if(op->timestamp, tid);
  }

  /**
   * Execute an insert action in n
   * op needs to be protected by hp
   */
  void do_node_insert(const pOp op, const pNode n, const std::size_t tid) {
    pNode child;
    // op->value != n->value because we made it to this node
    if (op->value < n->value) {
      child = n->left_child.load();
      if (child == nullptr) {
        // std::cout << "a " << op->value << " " << n->value << " " << tid << "\n"; 
        NodeState new_state(op->timestamp, 1, 0);
        pNode new_node = new Node<T>(max_threads_, 1, op->value, new_state);
        if (!n->left_child.compare_exchange_strong(child, new_node)) {
          delete new_node;
        } else {
          op->success.store(true);
        }
      } else {
        // std::cout << "b " << op->value << " " << child->value << " " << tid << "\n"; 
        if (!push_insert_to_child(op, child, tid))
          return;
      }
    } else if (op->value > n->value) {
      child = n->right_child.load();
      if (child == nullptr) {
        // std::cout << "a " << op->value << " " << n->value << " " << tid << "\n"; 
        NodeState new_state(op->timestamp, 1, 0);
        pNode new_node = new Node<T>(max_threads_, 1, op->value, new_state);
        if (!n->right_child.compare_exchange_strong(child, new_node)) {
          delete new_node;
        } else {
          op->success.store(true);
        }
      } else {
        // std::cout << "b " << op->value << " " << child->value << " " << tid << "\n"; 
        if (!push_insert_to_child(op, child, tid))
          return;
      }
    }

    n->ops.pop_if(op->timestamp, tid);
  }

  /**
   * Handle an insert operation in the child node, either push it to the child
   * Or if the child node has the value to be inserted reactive (if inactive) or stop the traversal of the operation
   * This function is here to reduce duplicated code
   * 
   * Returns false if the operation should not be removed from the queue of the parent of "child", true otherwise
   */
  bool push_insert_to_child(const pOp op, const pNode child, const std::size_t tid) {
    NodeState curr_state = child->state.load();

    if (child->value == op->value) {
      //node with desired value exists
      //ignore when the insert operation has already passed this node
      //reactive if it is a deactivated node
      if (curr_state.get_last_timestamp() >= op->timestamp) 
        return true;

      if (curr_state.get_active()) {
        op->split.store(op->value);            
      } else {
        NodeState new_state(op->timestamp, curr_state.all_children, curr_state.changes, true);

        if (child->state.compare_exchange_strong(curr_state, new_state)) {
          op->success.store(true);
        }
      }
    } else {
      //node value does not match
      //push operation to child
      op->to_visit.push(child, 0, tid);
      if (curr_state.get_last_timestamp() < op->timestamp) {
        NodeState new_state(op->timestamp, curr_state.all_children+1, curr_state.changes+1, curr_state.get_active());
        
        child->state.compare_exchange_strong(curr_state, new_state);
      }

      child->ops.push_if(op, tid);
      return true;
    }
    return true;
  }

  /**
   * Execute a lookup action in n
   * op needs to be protected by hp
   */
  void do_node_lookup(const pOp op, const pNode n, const std::size_t tid) {
    pNode child = n->right_child.load();
    if (op->value < n->value) 
      child = n->left_child.load();
    
    if (child != nullptr) {  
      NodeState curr_state = child->state.load();

      if (child->value == op->value) {
        if (curr_state.get_active() && curr_state.get_last_timestamp() < op->timestamp)
          op->success.store(true);
      } else {
        op->to_visit.push(child, 0, tid);
      }
      
      if (curr_state.get_last_timestamp() < op->timestamp) {
        NodeState new_state(op->timestamp, curr_state.all_children, curr_state.changes, curr_state.get_active());
        
        child->state.compare_exchange_strong(curr_state, new_state);
      }

      if (child->value != op->value)
        child->ops.push_if(op, tid);
    }
    n->ops.pop_if(op->timestamp, tid);
  }

  /**
   * Execute a remove action in n
   * Nodes are only marked as inactive and will be removed when the subtree is rebuild
   * op needs to be protected by hp
   */
  void do_node_remove(const pOp op, const pNode n, const std::size_t tid) {
    pNode child = n->right_child.load();
    if (op->value < n->value)
      child = n->left_child.load();

    if (child != nullptr) {
      NodeState curr_state = child->state.load();

      if (child->value != op->value) 
        op->to_visit.push(child, 0, tid);

      if (curr_state.get_last_timestamp() < op->timestamp) {
        NodeState new_state(op->timestamp, curr_state.all_children-1, curr_state.changes+1, curr_state.get_active() && child->value != op->value);

        child->state.compare_exchange_strong(curr_state, new_state);
      }

      if (child->value != op->value) 
        child->ops.push_if(op, tid);
    }

    n->ops.pop_if(op->timestamp, tid);
  }

  /**
   * Execute a range_count action in n
   * op needs to be protected by hp
   */
  void do_node_rangecount(const pOp op, const pNode n, const std::size_t tid) {
    if (op->split == T{}) {
      //This means n is not part of the result
      pNode child = n->left_child.load();
      if (child != nullptr) {
        if (child->value >= op->value && child->value <= op->value2) {
          //child is the top-most node included in the range, so the split point
          T cas_standin = T{};
          op->split.compare_exchange_strong(cas_standin, child->value);
          op->to_visit.push(child, 1, tid);
          child->ops.push_if(op, tid);
        } else if (n->value > op->value2) {
          op->to_visit.push(child, 0, tid);
          child->ops.push_if(op, tid);
        }
      }
      child = n->right_child.load();
      if (child != nullptr) {
        if (child->value >= op->value && child->value <= op->value2) {
          //child is the top-most node included in the range, so the split point
          T cas_standin = T{};
          op->split.compare_exchange_strong(cas_standin, child->value);
          op->to_visit.push(child, 1, tid);
          child->ops.push_if(op, tid);
        } else if (n->value < op->value) {
          op->to_visit.push(child, 0, tid);
          child->ops.push_if(op, tid);
        }
      }
    } else if (n->value == op->split) {
      //n is part of the results

      //push to left child
      pNode child = n->left_child.load();
      if (child != nullptr && n->value != op->value) {
        op->to_visit.push(child, child->value >= op->value, tid);
        child->ops.push_if(op, tid);
      }

      //push to right child
      child = n->right_child.load();
      if (child != nullptr && n->value != op->value2) {
        op->to_visit.push(child, child->value <= op->value2, tid);
        child->ops.push_if(op, tid);
      }

    } else if (n->value > op->split) {
      //operation has already been split and n is in the upper half
      handle_split_query(op, n, n->left_child.load(), n->right_child.load(), op->value2, tid, false, std::less<>{});
    } else {
      //operation has already been split and n is in the lower half
      handle_split_query(op, n, n->right_child.load(), n->left_child.load(), op->value, tid, true, std::greater<>{});
    }

    n->ops.pop_if(op->timestamp, tid);
  }

  /**
   * Handle a already split range count query in n
   * inner_child is closer to the split than n->value
   * outer_child is further away
   * comp_value is either the upper or lower bound of the operation depending on n->value and the split value of the operation
   * comp is the comparison operation between comp_value and n->value
   * lower indicates if the remaning results should be added to op->lower_value (true) or op->upper_value (false)
   */
  template <class Compare = std::less<>>
  void handle_split_query(const pOp op, const pNode n, const pNode inner_child, const pNode outer_child, const T comp_value, const std::size_t tid, bool lower, Compare&& comp = {}) {
    if (comp(n->value, comp_value)) {
      //whole inner child + push to outer child
      std::uint32_t inner_child_size = 0;
      if (inner_child != nullptr) {
        NodeState curr_state = inner_child->state.load();
        inner_child_size = curr_state.all_children;
      }

      if (outer_child != nullptr) {
        //only add one to the result, if outer child is part of it
        op->to_visit.push(outer_child, (comp(outer_child->value, comp_value) || outer_child->value == comp_value)+inner_child_size, tid);
        outer_child->ops.push_if(op, tid);
      } else {
        std::uint32_t cas_standin = 0;
        if (lower)
          op->lower_count.compare_exchange_strong(cas_standin, inner_child_size);
        else
          op->upper_count.compare_exchange_strong(cas_standin, inner_child_size);
      }
    } else if (n->value == comp_value) {
      //whole inner child
      if (inner_child != nullptr) {
        NodeState curr_state = inner_child->state.load();
        std::uint32_t cas_standin = 0;
        if (lower)
          op->lower_count.compare_exchange_strong(cas_standin, curr_state.all_children);
        else
          op->upper_count.compare_exchange_strong(cas_standin, curr_state.all_children);
      }
    } else {
      //only inner child
      if (inner_child != nullptr) {
        //only add one to the result, if inner child is part of it
        op->to_visit.push(inner_child, comp(inner_child->value, comp_value) || inner_child->value == comp_value, tid);
        inner_child->ops.push_if(op, tid);
      }
    }
  }

  /**
   * Rebuils the child of the (fake) root if neccessary
   * Returns false if the operation in the execute_until_timestamp_root function needs to be reloaded (bc this functions accessed other operations)
   * Returns true if operations does not need to be reloaded
   */
  bool rebuild_root(const std::uint64_t timestamp, const std::size_t tid) {
    pNode child = fake_root_child.load();
    if (child == nullptr)
      return true;
    NodeState curr_state = child->state.load();
    if (curr_state.changes > child->init_size/2 && (curr_state.all_children > 5 || child->init_size > 5)) {
      std::pair<pNode, bool> new_node_b = rebuild(child, timestamp, tid);
      if (!new_node_b.second)
        return false;
      if (!fake_root_child.compare_exchange_strong(child, new_node_b.first)) {
        delete_tree(new_node_b.first);
        return false;
      } else {
        to_be_deleted_.push({set_mask_.load(), child}, tid);
        to_be_deleted_num_.fetch_add(1);
        return false;
      }
    }
    return true;
  }

  /**
   * Rebuils the childdren of the n if neccessary
   * Returns false if the operation in the execute_until_timestamp function needs to be reloaded (bc this functions accessed other operations)
   * Returns true if operations does not need to be reloaded
   */
  bool rebuild_node(const pNode n, const std::uint64_t timestamp, const std::size_t tid) {
    bool need_to_reload = false;

    pNode child = n->left_child.load();
    if (child != nullptr) {
      NodeState curr_state = child->state.load();
      if (curr_state.changes > child->init_size/2 && (curr_state.all_children > 5 || child->init_size > 5)) {
        std::pair<pNode, bool> new_node_b = rebuild(child, timestamp, tid);
        if (!new_node_b.second)
          return false;
        if (!n->left_child.compare_exchange_strong(child, new_node_b.first)) {
          delete_tree(new_node_b.first);
          return false;
        } else {
          to_be_deleted_.push({set_mask_.load(), child}, tid);
          to_be_deleted_num_.fetch_add(1);
          need_to_reload = true;
        }
      }
    }

    child = n->right_child.load();
    if (child != nullptr) {
      NodeState curr_state = child->state.load();
      if (curr_state.changes > child->init_size/2 && (curr_state.all_children > 5 || child->init_size > 5)) {
        std::pair<pNode, bool> new_node_b = rebuild(child, timestamp, tid);
        if (!new_node_b.second)
          return false;
        if (!n->right_child.compare_exchange_strong(child, new_node_b.first)){
          delete_tree(new_node_b.first);
          return false;
        } else {
          to_be_deleted_.push({set_mask_.load(), child}, tid);
          to_be_deleted_num_.fetch_add(1);
          need_to_reload = true;
        }
      }
    }
    return !need_to_reload;
  }

  /**
   * Rebuild the subtree rooted at n
   * The rebuild is triggered by an operation with timestamp "timestamp"
   * So all operations until that timestamp will be completed in the subtree and it will be initalized with timestmap-1 as last seen timestamp
   * This allows the triggering operation to still traverse the subtree later on
   */
  std::pair<pNode, bool> rebuild(const pNode n, const std::uint64_t timestamp, const std::size_t tid) {
    NodeState curr_state = n->state.load();
    
    std::vector<T> values;
    values.reserve(n->init_size + curr_state.changes);
    std::queue<pNode> to_be_done;
    to_be_done.push(n);

    //do to traversals of the subtree twice like it is porposed in the paper
    //finish all operations
    while (!to_be_done.empty()) {
      auto a = to_be_done.front();
      to_be_done.pop();

      execute_until_timestamp(a, timestamp, tid);

      pNode child = a->left_child.load();
      if (child != nullptr)
        to_be_done.push(child);
      child = a->right_child.load();
      if (child != nullptr)
        to_be_done.push(child);
    }
    //collect all active nodes
    to_be_done.push(n);
    while (!to_be_done.empty()) {
      auto a = to_be_done.front();
      to_be_done.pop();

      curr_state = a->state.load();

      if (curr_state.get_active())
        values.emplace_back(a->value);

      pNode child = a->left_child.load();
      if (child != nullptr)
        to_be_done.push(child);
      child = a->right_child.load();
      if (child != nullptr)
        to_be_done.push(child);
    }

    std::sort(values.begin(), values.end());

    if (values.size() == 0) {
      return {nullptr, true};
    }
    return {build_tree(values, 0, values.size()-1, timestamp), true};
  }

  /**
   * Build a perfectly balanced binary tree from the values[left:right+1] (in python notation)
   * The rebuild is triggered by an operation with timestamp "timestamp"
   */
  pNode build_tree(std::vector<T>& values, std::size_t left, std::size_t right, const std::uint64_t timestamp) {
    if (left > right) return nullptr;
    std::size_t middle = left+((right-left)/2);
    NodeState init_state(timestamp-1, static_cast<std::uint32_t>(right-left+1), 0);
    pNode new_node = new Node<T>(max_threads_, right-left+1, values[middle], init_state);
    pNode left_child = nullptr;
    if (middle != 0) {
      left_child = build_tree(values, left, middle-1, timestamp);
    }
    pNode right_child = build_tree(values, middle+1, right, timestamp);

    new_node->left_child.store(left_child);
    new_node->right_child.store(right_child);

    return new_node;
  }

  /**
   * Delete the whole subtree rooted at del
   */
  void delete_tree(pNode del) {
    std::queue<pNode> q;
    q.push(del);
    while (!q.empty()) {
      auto n = q.front();
      q.pop();
      if (n->left_child.load() != nullptr)
        q.push(n->left_child.load());
      if (n->right_child.load() != nullptr)
        q.push(n->right_child.load());
      std::atomic_thread_fence(std::memory_order_seq_cst);
      delete n;
    }
  }
};