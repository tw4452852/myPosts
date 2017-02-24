/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <pthread.h>

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include "pthread_internal.h"

#include "private/bionic_macros.h"
#include "private/bionic_ssp.h"
#include "private/bionic_tls.h"
#include "private/libc_logging.h"
#include "private/ErrnoRestorer.h"
#include "private/ScopedPthreadMutexLocker.h"

// x86 uses segment descriptors rather than a direct pointer to TLS.
#if __i386__
#include <asm/ldt.h>
extern "C" __LIBC_HIDDEN__ void __init_user_desc(struct user_desc*, int, void*);
#endif

extern "C" int __isthreaded;

// This code is used both by each new pthread and the code that initializes the main thread.
void __init_tls(pthread_internal_t* thread) {
	...
  thread->tls[TLS_SLOT_THREAD_ID] = thread;
	...
}

void __init_alternate_signal_stack(pthread_internal_t* thread) {
  // Create and set an alternate signal stack.
  stack_t ss;
  ss.ss_sp = mmap(NULL, SIGSTKSZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (ss.ss_sp != MAP_FAILED) {
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    thread->alternate_signal_stack = ss.ss_sp;
  }
}

int __init_thread(pthread_internal_t* thread, bool add_to_thread_list) {
  int error = 0;

  // Set the scheduling policy/priority of the thread.
  if (thread->attr.sched_policy != SCHED_NORMAL) {
    sched_param param;
    param.sched_priority = thread->attr.sched_priority;
    if (sched_setscheduler(thread->tid, thread->attr.sched_policy, &param) == -1) {
#if __LP64__
      // For backwards compatibility reasons, we only report failures on 64-bit devices.
      error = errno;
#endif
      __libc_format_log(ANDROID_LOG_WARN, "libc",
                        "pthread_create sched_setscheduler call failed: %s", strerror(errno));
    }
  }

  thread->cleanup_stack = NULL;

  if (add_to_thread_list) {
    _pthread_internal_add(thread);
  }

  return error;
}

static void* __create_thread_stack(pthread_internal_t* thread) {
  // Create a new private anonymous map.
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
  void* stack = mmap(NULL, thread->attr.stack_size, prot, flags, -1, 0);
  if (stack == MAP_FAILED) {
    __libc_format_log(ANDROID_LOG_WARN,
                      "libc",
                      "pthread_create failed: couldn't allocate %zd-byte stack: %s",
                      thread->attr.stack_size, strerror(errno));
    return NULL;
  }

  // Set the guard region at the end of the stack to PROT_NONE.
  if (mprotect(stack, thread->attr.guard_size, PROT_NONE) == -1) {
    __libc_format_log(ANDROID_LOG_WARN, "libc",
                      "pthread_create failed: couldn't mprotect PROT_NONE %zd-byte stack guard region: %s",
                      thread->attr.guard_size, strerror(errno));
    munmap(stack, thread->attr.stack_size);
    return NULL;
  }

  return stack;
}

static int __pthread_start(void* arg) {
  pthread_internal_t* thread = reinterpret_cast<pthread_internal_t*>(arg);

  // Wait for our creating thread to release us. This lets it have time to
  // notify gdb about this thread before we start doing anything.
  // This also provides the memory barrier needed to ensure that all memory
  // accesses previously made by the creating thread are visible to us.
  pthread_mutex_lock(&thread->startup_handshake_mutex);
  pthread_mutex_destroy(&thread->startup_handshake_mutex);

  __init_alternate_signal_stack(thread);

  void* result = thread->start_routine(thread->start_routine_arg);
  pthread_exit(result);

  return 0;
}

// A dummy start routine for pthread_create failures where we've created a thread but aren't
// going to run user code on it. We swap out the user's start routine for this and take advantage
// of the regular thread teardown to free up resources.
static void* __do_nothing(void*) {
  return NULL;
}

int pthread_create(pthread_t* thread_out, pthread_attr_t const* attr,
                   void* (*start_routine)(void*), void* arg) {
	...

  pthread_internal_t* thread = reinterpret_cast<pthread_internal_t*>(calloc(sizeof(*thread), 1));
  if (thread == NULL) {
    __libc_format_log(ANDROID_LOG_WARN, "libc", "pthread_create failed: couldn't allocate thread");
    return EAGAIN;
  }

  ...

  __init_tls(thread);

  // Create a mutex for the thread in TLS to wait on once it starts so we can keep
  // it from doing anything until after we notify the debugger about it
  //
  // This also provides the memory barrier we need to ensure that all
  // memory accesses previously performed by this thread are visible to
  // the new thread.
  pthread_mutex_init(&thread->startup_handshake_mutex, NULL);
  pthread_mutex_lock(&thread->startup_handshake_mutex);

  thread->start_routine = start_routine;
  thread->start_routine_arg = arg;

  thread->set_cached_pid(getpid());

  int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
      CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;
  void* tls = thread->tls;
#if defined(__i386__)
  // On x86 (but not x86-64), CLONE_SETTLS takes a pointer to a struct user_desc rather than
  // a pointer to the TLS itself.
  user_desc tls_descriptor;
  __init_user_desc(&tls_descriptor, false, tls);
  tls = &tls_descriptor;
#endif
  int rc = clone(__pthread_start, child_stack, flags, thread, &(thread->tid), tls, &(thread->tid));
  if (rc == -1) {
    int clone_errno = errno;
    // We don't have to unlock the mutex at all because clone(2) failed so there's no child waiting to
    // be unblocked, but we're about to unmap the memory the mutex is stored in, so this serves as a
    // reminder that you can't rewrite this function to use a ScopedPthreadMutexLocker.
    pthread_mutex_unlock(&thread->startup_handshake_mutex);
    if (!thread->user_allocated_stack()) {
      munmap(thread->attr.stack_base, thread->attr.stack_size);
    }
    free(thread);
    __libc_format_log(ANDROID_LOG_WARN, "libc", "pthread_create failed: clone failed: %s", strerror(errno));
    return clone_errno;
  }

  int init_errno = __init_thread(thread, true);
  if (init_errno != 0) {
    // Mark the thread detached and replace its start_routine with a no-op.
    // Letting the thread run is the easiest way to clean up its resources.
    thread->attr.flags |= PTHREAD_ATTR_FLAG_DETACHED;
    thread->start_routine = __do_nothing;
    pthread_mutex_unlock(&thread->startup_handshake_mutex);
    return init_errno;
  }

  // Publish the pthread_t and unlock the mutex to let the new thread start running.
  *thread_out = reinterpret_cast<pthread_t>(thread);
  pthread_mutex_unlock(&thread->startup_handshake_mutex);

  return 0;
}
