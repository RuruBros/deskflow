/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2022 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <Carbon/Carbon.h>
#include <dispatch/dispatch.h>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <type_traits>
#include <utility>

using CFDeallocator = decltype(&CFRelease);
using AutoCFArray = std::unique_ptr<const __CFArray, CFDeallocator>;
using AutoCFDictionary = std::unique_ptr<const __CFDictionary, CFDeallocator>;
using AutoTISInputSourceRef = std::unique_ptr<__TISInputSource, CFDeallocator>;

inline std::mutex g_tisMutex;

// TIS shares input-source state with AppKit and must not be called concurrently from the core thread.
template <typename Callable> std::invoke_result_t<Callable> runTISOnMainThread(Callable &&callable)
{
  using Result = std::invoke_result_t<Callable>;

  auto invoke = [function = std::forward<Callable>(callable)]() mutable -> Result {
    std::lock_guard<std::mutex> lock(g_tisMutex);
    return std::invoke(function);
  };

  if (pthread_main_np() != 0) {
    return invoke();
  }

  std::packaged_task<Result()> task(std::move(invoke));
  auto result = task.get_future();
  dispatch_sync_f(dispatch_get_main_queue(), &task, [](void *context) {
    (*static_cast<std::packaged_task<Result()> *>(context))();
  });
  return result.get();
}
