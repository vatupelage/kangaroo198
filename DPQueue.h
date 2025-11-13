/*
 * This file is part of the BSGS distribution (https://github.com/JeanLucPons/Kangaroo).
 * Copyright (c) 2020 Jean Luc PONS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DPQUEUEH
#define DPQUEUEH

#include <queue>
#include <vector>
#include "HashTable.h"

#ifdef WIN64
#include <windows.h>
#else
#include <pthread.h>
#endif

// Thread-safe queue for distinguished points
// Used to decouple GPU computation from network I/O
class DPQueue {

public:

  DPQueue() {
#ifdef WIN64
    mutex = CreateMutex(NULL, FALSE, NULL);
    notEmpty = CreateEvent(NULL, FALSE, FALSE, NULL);
#else
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&notEmpty, NULL);
#endif
    shutdown = false;
    totalPushed = 0;
    totalPopped = 0;
  }

  ~DPQueue() {
#ifdef WIN64
    CloseHandle(mutex);
    CloseHandle(notEmpty);
#else
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&notEmpty);
#endif
  }

  // Push a single DP (non-blocking, instant for GPU thread)
  void push(const ITEM& dp, uint32_t threadId, uint32_t gpuId) {
    DPITEM item;
    item.dp = dp;
    item.threadId = threadId;
    item.gpuId = gpuId;

#ifdef WIN64
    WaitForSingleObject(mutex, INFINITE);
#else
    pthread_mutex_lock(&mutex);
#endif

    queue.push(item);
    totalPushed++;

#ifdef WIN64
    SetEvent(notEmpty);
    ReleaseMutex(mutex);
#else
    pthread_cond_signal(&notEmpty);
    pthread_mutex_unlock(&mutex);
#endif
  }

  // Push batch of DPs (non-blocking, instant for GPU thread)
  void push_batch(const std::vector<ITEM>& dps, uint32_t threadId, uint32_t gpuId) {
    if (dps.empty())
      return;

#ifdef WIN64
    WaitForSingleObject(mutex, INFINITE);
#else
    pthread_mutex_lock(&mutex);
#endif

    for (size_t i = 0; i < dps.size(); i++) {
      DPITEM item;
      item.dp = dps[i];
      item.threadId = threadId;
      item.gpuId = gpuId;
      queue.push(item);
    }
    totalPushed += dps.size();

#ifdef WIN64
    SetEvent(notEmpty);
    ReleaseMutex(mutex);
#else
    pthread_cond_signal(&notEmpty);
    pthread_mutex_unlock(&mutex);
#endif
  }

  // Pop batch of DPs (blocking for network thread)
  // Returns up to maxCount DPs, or waits if queue is empty
  bool pop_batch(std::vector<ITEM>& dps, std::vector<uint32_t>& threadIds,
                 std::vector<uint32_t>& gpuIds, uint32_t maxCount, double timeout_sec = 1.0) {
    dps.clear();
    threadIds.clear();
    gpuIds.clear();

#ifdef WIN64
    WaitForSingleObject(mutex, INFINITE);

    // Wait for data or shutdown
    while (queue.empty() && !shutdown) {
      ReleaseMutex(mutex);
      DWORD result = WaitForSingleObject(notEmpty, (DWORD)(timeout_sec * 1000));
      WaitForSingleObject(mutex, INFINITE);

      if (result == WAIT_TIMEOUT) {
        ReleaseMutex(mutex);
        return false;
      }
    }

    if (shutdown && queue.empty()) {
      ReleaseMutex(mutex);
      return false;
    }

    // Pop up to maxCount items
    while (!queue.empty() && dps.size() < maxCount) {
      DPITEM item = queue.front();
      queue.pop();
      dps.push_back(item.dp);
      threadIds.push_back(item.threadId);
      gpuIds.push_back(item.gpuId);
      totalPopped++;
    }

    ReleaseMutex(mutex);
#else
    pthread_mutex_lock(&mutex);

    // Wait for data or shutdown
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)timeout_sec;
    ts.tv_nsec += (long)((timeout_sec - (time_t)timeout_sec) * 1e9);
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000000000L;
    }

    while (queue.empty() && !shutdown) {
      int result = pthread_cond_timedwait(&notEmpty, &mutex, &ts);
      if (result == ETIMEDOUT) {
        pthread_mutex_unlock(&mutex);
        return false;
      }
    }

    if (shutdown && queue.empty()) {
      pthread_mutex_unlock(&mutex);
      return false;
    }

    // Pop up to maxCount items
    while (!queue.empty() && dps.size() < maxCount) {
      DPITEM item = queue.front();
      queue.pop();
      dps.push_back(item.dp);
      threadIds.push_back(item.threadId);
      gpuIds.push_back(item.gpuId);
      totalPopped++;
    }

    pthread_mutex_unlock(&mutex);
#endif

    return !dps.empty();
  }

  // Check if queue is empty
  bool empty() {
#ifdef WIN64
    WaitForSingleObject(mutex, INFINITE);
    bool result = queue.empty();
    ReleaseMutex(mutex);
#else
    pthread_mutex_lock(&mutex);
    bool result = queue.empty();
    pthread_mutex_unlock(&mutex);
#endif
    return result;
  }

  // Get current queue size
  size_t size() {
#ifdef WIN64
    WaitForSingleObject(mutex, INFINITE);
    size_t result = queue.size();
    ReleaseMutex(mutex);
#else
    pthread_mutex_lock(&mutex);
    size_t result = queue.size();
    pthread_mutex_unlock(&mutex);
#endif
    return result;
  }

  // Signal shutdown to network thread
  void requestShutdown() {
#ifdef WIN64
    WaitForSingleObject(mutex, INFINITE);
    shutdown = true;
    SetEvent(notEmpty);
    ReleaseMutex(mutex);
#else
    pthread_mutex_lock(&mutex);
    shutdown = true;
    pthread_cond_broadcast(&notEmpty);
    pthread_mutex_unlock(&mutex);
#endif
  }

  // Get statistics
  uint64_t getTotalPushed() const { return totalPushed; }
  uint64_t getTotalPopped() const { return totalPopped; }

private:

  struct DPITEM {
    ITEM dp;
    uint32_t threadId;
    uint32_t gpuId;
  };

  std::queue<DPITEM> queue;
  bool shutdown;
  uint64_t totalPushed;
  uint64_t totalPopped;

#ifdef WIN64
  HANDLE mutex;
  HANDLE notEmpty;
#else
  pthread_mutex_t mutex;
  pthread_cond_t notEmpty;
#endif

};

#endif // DPQUEUEH
