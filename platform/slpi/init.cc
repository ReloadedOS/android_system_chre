/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <type_traits>

extern "C" {

#include "qurt.h"

}  // extern "C"

#include "chre/apps/gnss_world/gnss_world.h"
#include "chre/apps/imu_cal/imu_cal.h"
#include "chre/apps/message_world/message_world.h"
#include "chre/apps/sensor_world/sensor_world.h"
#include "chre/apps/timer_world/timer_world.h"
#include "chre/apps/wifi_world/wifi_world.h"
#include "chre/core/event_loop.h"
#include "chre/core/event_loop_manager.h"
#include "chre/core/init.h"
#include "chre/core/nanoapp.h"
#include "chre/platform/log.h"
#include "chre/platform/memory.h"
#include "chre/platform/mutex.h"
#include "chre/platform/platform_nanoapp.h"
#include "chre/platform/slpi/fastrpc.h"
#include "chre/util/lock_guard.h"

using chre::EventLoop;
using chre::LockGuard;
using chre::Mutex;

extern "C" int chre_slpi_stop_thread(void);

namespace {

//! Size of the stack for the CHRE thread, in bytes.
constexpr size_t kStackSize = (8 * 1024);

//! Memory partition where the thread control block (TCB) should be stored,
//! which controls micro-image support (0 = big image, 1 = micro image).
//! @see qurt_thread_attr_set_tcb_partition
constexpr unsigned char kTcbPartition = 0;

//! How long we wait (in microseconds) between checks on whether the CHRE thread
//! has exited after we invoked stop().
constexpr qurt_timer_duration_t kThreadStatusPollingIntervalUsec = 5000;  // 5ms

//! Pointer to the main CHRE event loop. Modification must only be done while
//! the CHRE thread is stopped, and while holding gThreadMutex.
EventLoop *gEventLoop;

//! Buffer to use for the CHRE thread's stack.
typename std::aligned_storage<kStackSize>::type gStack;

//! QuRT OS handle for the CHRE thread.
qurt_thread_t gThreadHandle;

//! Protects access to thread metadata, like gThreadRunning, during critical
//! sections (starting/stopping the CHRE thread).
Mutex *gThreadMutex;
typename std::aligned_storage<sizeof(Mutex), alignof(Mutex)>::type
    gThreadMutexStorage;

//! Set to true when the CHRE thread starts, and false when it exits normally.
bool gThreadRunning;

//! A thread-local storage key, which is currently only used to add a thread
//! destructor callback for the host FastRPC thread.
int gTlsKey;
bool gTlsKeyValid;

// TODO: We would prefer to just use staitc global C++ constructor/destructor
// support, but currently, destructors do not seem to get called. These work as
// a temporary workaround, though.
__attribute__((constructor))
void onLoad(void) {
  gThreadMutex = new(&gThreadMutexStorage) Mutex();
}

__attribute__((destructor))
void onUnload(void) {
  gThreadMutex->~Mutex();
  gThreadMutex = nullptr;
}

/**
 * Entry point for the QuRT thread that runs CHRE.
 *
 * @param data Argument passed to qurt_thread_create()
 */
void chreThreadEntry(void * /*data*/) {
  chre::init();
  gEventLoop = chre::EventLoopManagerSingleton::get()->createEventLoop();
  if (gEventLoop == nullptr) {
    LOGE("Failed to create event loop!");
  } else {
    // TODO: work up a better way of handling built-in nanoapps (or don't have
    // any at all). Also, sensor world test functionality should be replaced by
    // a more automated framework.
#if 0
    chre::PlatformNanoapp gnssWorldPlatformNanoapp;
    gnssWorldPlatformNanoapp.mStart = chre::app::gnssWorldStart;
    gnssWorldPlatformNanoapp.mHandleEvent = chre::app::gnssWorldHandleEvent;
    gnssWorldPlatformNanoapp.mStop = chre::app::gnssWorldStop;
    gEventLoop->startNanoapp(&gnssWorldPlatformNanoapp);
#endif

#if 0
    chre::PlatformNanoapp imuCalPlatformNanoapp;
    imuCalPlatformNanoapp.mStart = chre::app::imuCalStart;
    imuCalPlatformNanoapp.mHandleEvent = chre::app::imuCalHandleEvent;
    imuCalPlatformNanoapp.mStop = chre::app::imuCalStop;
    gEventLoop->startNanoapp(&imuCalPlatformNanoapp);
#endif

#if 0
    chre::PlatformNanoapp sensorWorldPlatformNanoapp;
    sensorWorldPlatformNanoapp.mStart = chre::app::sensorWorldStart;
    sensorWorldPlatformNanoapp.mHandleEvent = chre::app::sensorWorldHandleEvent;
    sensorWorldPlatformNanoapp.mStop = chre::app::sensorWorldStop;
    gEventLoop->startNanoapp(&sensorWorldPlatformNanoapp);
#endif

#if 0
    chre::PlatformNanoapp messageWorldPlatformNanoapp;
    messageWorldPlatformNanoapp.mStart = chre::app::messageWorldStart;
    messageWorldPlatformNanoapp.mHandleEvent =
        chre::app::messageWorldHandleEvent;
    messageWorldPlatformNanoapp.mStop = chre::app::messageWorldStop;
    gEventLoop->startNanoapp(&messageWorldPlatformNanoapp);
#endif

#if 0
    chre::PlatformNanoapp timerWorldPlatformNanoapp;
    timerWorldPlatformNanoapp.mStart = chre::app::timerWorldStart;
    timerWorldPlatformNanoapp.mHandleEvent = chre::app::timerWorldHandleEvent;
    timerWorldPlatformNanoapp.mStop = chre::app::timerWorldStop;
    gEventLoop->startNanoapp(&timerWorldPlatformNanoapp);
#endif

#if 0
    chre::PlatformNanoapp wifiWorldPlatformNanoapp;
    wifiWorldPlatformNanoapp.mStart = chre::app::wifiWorldStart;
    wifiWorldPlatformNanoapp.mHandleEvent = chre::app::wifiWorldHandleEvent;
    wifiWorldPlatformNanoapp.mStop = chre::app::wifiWorldStop;
    gEventLoop->startNanoapp(&wifiWorldPlatformNanoapp);
#endif

    gEventLoop->run();
  }

  chre::deinit();
  gEventLoop = nullptr;
  gThreadRunning = false;
  LOGD("CHRE thread exiting");
}

void onHostProcessTerminated(void * /*data*/) {
  LOGW("Host process died, exiting CHRE (running %d)", gThreadRunning);
  chre_slpi_stop_thread();
}

}  // anonymous namespace

namespace chre {

EventLoop *getCurrentEventLoop() {
  return (qurt_thread_get_id() == gThreadHandle) ? gEventLoop : nullptr;
}

}  // namespace chre

/**
 * Invoked over FastRPC to initialize and start the CHRE thread.
 *
 * @return 0 on success, nonzero on failure (per FastRPC requirements)
 */
extern "C" int chre_slpi_start_thread(void) {
  // This lock ensures that we only start the thread once
  LockGuard<Mutex> lock(*gThreadMutex);
  int fastRpcResult = CHRE_FASTRPC_ERROR;

  if (gThreadRunning) {
    LOGE("CHRE thread already running");
  } else {
    // Human-readable name for the CHRE thread (not const in QuRT API, but they
    // make a copy)
    char threadName[] = "CHRE";
    qurt_thread_attr_t attributes;

    qurt_thread_attr_init(&attributes);
    qurt_thread_attr_set_stack_addr(&attributes, &gStack);
    qurt_thread_attr_set_stack_size(&attributes, kStackSize);
    qurt_thread_attr_set_name(&attributes, threadName);
    qurt_thread_attr_set_tcb_partition(&attributes, kTcbPartition);

    int result = qurt_thread_create(&gThreadHandle, &attributes,
                                    chreThreadEntry, nullptr);
    if (result != QURT_EOK) {
      LOGE("Couldn't create CHRE thread: %d", result);
    } else {
      LOGD("Started CHRE thread");
      gThreadRunning = true;
      fastRpcResult = CHRE_FASTRPC_SUCCESS;
    }
  }

  return fastRpcResult;
}

/**
 * Blocks until the CHRE thread exits. Called over FastRPC to monitor for
 * abnormal termination of the CHRE thread and/or SLPI as a whole.
 *
 * @return Always returns 0, indicating success (per FastRPC requirements)
 */
extern "C" int chre_slpi_wait_on_thread_exit(void) {
  if (!gThreadRunning) {
    LOGE("Tried monitoring for CHRE thread exit, but thread not running!");
  } else {
    int status;
    int result = qurt_thread_join(gThreadHandle, &status);
    if (result != QURT_EOK) {
      LOGE("qurt_thread_join failed with result %d", result);
    }
    LOGI("Detected CHRE thread exit");
  }

  return CHRE_FASTRPC_SUCCESS;
}

/**
 * If the CHRE thread is running, requests it to perform graceful shutdown,
 * waits for it to exit, then completes teardown.
 *
 * @return Always returns 0, indicating success (per FastRPC requirements)
 */
extern "C" int chre_slpi_stop_thread(void) {
  // This lock ensures that we will complete shutdown before the thread can be
  // started again
  LockGuard<Mutex> lock(*gThreadMutex);

  if (!gThreadRunning || gEventLoop == nullptr) {
    LOGD("Tried to stop CHRE thread, but not running");
  } else {
    gEventLoop->stop();

    // Poll until the thread has stopped; note that we can't use
    // qurt_thread_join() here because chreMonitorThread() will already be
    // blocking in it, and attempting to join the same target from two threads
    // is invalid. Technically, we could use a condition variable, but this is
    // simpler and we don't care too much about being notified right away.
    while (gThreadRunning) {
      qurt_timer_sleep(kThreadStatusPollingIntervalUsec);
    }
    gThreadHandle = 0;

    // TODO: need to figure out the right place to put this, to make sure we're
    // not trying to post events to an EventLoop that is already stopped, etc.
    // Becomes even trickier when log messages get routed through the HostLink.
    chre::HostLinkBase::shutdown();

    if (gTlsKeyValid) {
      int ret = qurt_tls_delete_key(gTlsKey);
      if (ret != QURT_EOK) {
        // Note: LOGE is not necessarily safe to use after stopping CHRE
        FARF(ERROR, "Deleting TLS key failed: %d", ret);
      }
      gTlsKeyValid = false;
    }
  }

  return CHRE_FASTRPC_SUCCESS;
}

/**
 * Creates a thread-local storage (TLS) key in QuRT, which we use to inject a
 * destructor that is called when the current FastRPC thread terminates. This is
 * used to get a notification when the original FastRPC thread dies for any
 * reason, so we can stop the CHRE thread.
 *
 * Note that this needs to be invoked from a separate thread on the host process
 * side. It doesn't work if called from a thread that will be blocking inside a
 * FastRPC call, such as the monitor thread.
 *
 * @return 0 on success, nonzero on failure (per FastRPC requirements)
 */
extern "C" int chre_slpi_initialize_reverse_monitor(void) {
  LockGuard<Mutex> lock(*gThreadMutex);

  if (!gTlsKeyValid) {
    int result = qurt_tls_create_key(&gTlsKey, onHostProcessTerminated);
    if (result != QURT_EOK) {
      LOGE("Couldn't create TLS key: %d", result);
    } else {
      // We need to set the value to something for the destructor to be invoked
      result = qurt_tls_set_specific(gTlsKey, &gTlsKey);
      if (result != QURT_EOK) {
        LOGE("Couldn't set TLS data: %d", result);
        qurt_tls_delete_key(gTlsKey);
      } else {
        gTlsKeyValid = true;
      }
    }
  }

  return (gTlsKeyValid) ? CHRE_FASTRPC_SUCCESS : CHRE_FASTRPC_ERROR;
}

// TODO: Remove this stub once the symbol is provided.
extern "C" void __cxa_finalize(void *) {}
