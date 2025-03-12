/******************************************************************************
 * Copyright (c) 2014-2016, Pedro Ramalhete, Andreia Correia
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Concurrency Freaks nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.

 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************
 */

#pragma once

#include <iostream>
#include <functional>
#include <vector>

#include <boost/atomic/atomic.hpp>

/**
 * Hazard Pointer class with some adapations to save some memory
 * Class T has to have a member next of type T* and a member value of type V
 * Object only get deleted if o->next = nullptr and o->next = V{}
 * See original authors above
 */
template<typename T, typename V>
class ConditionalHazardPointers {

private:
  // static const std::size_t      HP_MAX_THREADS = 128;
  // static const std::size_t      HP_MAX_HPS = 128;     // This is named 'K' in the HP paper
  // static const std::size_t      CLPAD = 128/sizeof(boost::atomic<T*>);
  static const std::size_t      HP_THRESHOLD_R = 0; // This is named 'R' in the HP paper
  // static const std::size_t      MAX_RETIRED = HP_MAX_THREADS*HP_MAX_HPS; // Maximum number of retired objects per thread

  const std::size_t             maxHPs_;
  const std::size_t             maxThreads_;

  std::vector<std::vector<boost::atomic<T*>>> hp;
  // It's not nice that we have a lot of empty vectors, but we need padding to avoid false sharing
  std::vector<std::vector<T*>> retiredList;
public:

  ConditionalHazardPointers(std::size_t maxHPs, std::size_t maxThreads) : maxHPs_{maxHPs}, 
                                                               maxThreads_{maxThreads}, 
                                                               hp(maxThreads_), 
                                                               retiredList(maxThreads_) {
    for (std::size_t i = 0; i < maxThreads_; ++i) {
      hp[i] = std::vector<boost::atomic<T*>>(maxHPs_);
      for (std::size_t j = 0; j < maxHPs_; ++j) {
        hp[i][j].store(nullptr);
      }
    }
  }

  ~ConditionalHazardPointers() {
    for (std::size_t ithread = 0; ithread < maxThreads_; ithread++) {
      // Clear the current retired nodes
      for (std::size_t iret = 0; iret < retiredList[ithread].size(); iret++) {
        delete retiredList[ithread][iret];
      }
    }
  }


  /**
   * Progress Condition: wait-free bounded (by maxHPs_)
   */
  void clear(const std::size_t tid) {
    for (std::size_t ihp = 0; ihp < maxHPs_; ihp++) {
      hp[tid][ihp].store(nullptr);
    }
  }


  /**
   * Progress Condition: wait-free population oblivious
   */
  void clearOne(std::size_t ihp, const std::size_t tid) {
    hp[tid][ihp].store(nullptr);
  }


  /**
   * Progress Condition: lock-free
   */
  T* protect(std::size_t index, const boost::atomic<T*>& atom, const std::size_t tid) {
    T* n = nullptr;
    T* ret;
    while ((ret = atom.load()) != n) {
      hp[tid][index].store(ret);
      n = ret;
    }
    return ret;
  }

  T* get(std::size_t index, const std::size_t tid){
    return hp[tid][index].load();
  }
  /**
   * This returns the same value that is passed as ptr, which is sometimes useful
   * Progress Condition: wait-free population oblivious
   */
  T* protectPtr(std::size_t index, T* ptr, const std::size_t tid) {
    hp[tid][index].store(ptr);
    return ptr;
  }



  /**
   * This returns the same value that is passed as ptr, which is sometimes useful
   * Progress Condition: wait-free population oblivious
   */
  T* protectPtrRelease(std::size_t index, T* ptr, const std::size_t tid) {
    hp[tid][index].store(ptr);
    return ptr;
  }


  /**
   * Progress Condition: wait-free bounded (by the number of threads squared)
   */
  void retire(T* ptr, std::size_t tid) {
    retiredList[tid].push_back(ptr);
    if (retiredList[tid].size() < HP_THRESHOLD_R) return;
    for (unsigned iret = 0; iret < retiredList[tid].size();) {
      auto obj = retiredList[tid][iret];
      bool canDelete = true;
      for (std::size_t i = 0; i < maxThreads_ && canDelete; ++i) {
        for (std::size_t ihp = 0; ihp < maxHPs_; ++ihp) {
          if (hp[i][ihp].load() == obj) {
            canDelete = false;
            break;
          }
        }
      }
      if (canDelete && obj->value.load() == V{} && obj->next == nullptr) {
        retiredList[tid].erase(retiredList[tid].begin() + iret);
        delete obj;
        // std::clog << tid << " freed " << obj << std::endl;
        continue;
      }
      iret++;
    }
  }
};