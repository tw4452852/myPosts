/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include "linker.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <android/dlext.h>

#include <bionic/pthread_internal.h>
#include "private/bionic_tls.h"
#include "private/ScopedPthreadMutexLocker.h"
#include "private/ThreadLocalBuffer.h"

/* This file hijacks the symbols stubbed out in libdl.so. */

static pthread_mutex_t g_dl_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;

static const char* __bionic_set_dlerror(char* new_value) {
  char** dlerror_slot = &reinterpret_cast<char**>(__get_tls())[TLS_SLOT_DLERROR];

  const char* old_value = *dlerror_slot;
  *dlerror_slot = new_value;
  return old_value;
}

static void __bionic_format_dlerror(const char* msg, const char* detail) {
  char* buffer = __get_thread()->dlerror_buffer;
  strlcpy(buffer, msg, __BIONIC_DLERROR_BUFFER_SIZE);
  if (detail != nullptr) {
    strlcat(buffer, ": ", __BIONIC_DLERROR_BUFFER_SIZE);
    strlcat(buffer, detail, __BIONIC_DLERROR_BUFFER_SIZE);
  }

  __bionic_set_dlerror(buffer);
}

const char* dlerror() {
  const char* old_value = __bionic_set_dlerror(nullptr);
  return old_value;
}

void android_get_LD_LIBRARY_PATH(char* buffer, size_t buffer_size) {
	int tid = gettid();
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: enter\n", tid, __func__);
  ScopedPthreadMutexLocker locker(&g_dl_mutex);
  do_android_get_LD_LIBRARY_PATH(buffer, buffer_size);
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: return\n", tid, __func__);
}

void android_update_LD_LIBRARY_PATH(const char* ld_library_path) {
	int tid = gettid();
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: enter\n", tid, __func__);
  ScopedPthreadMutexLocker locker(&g_dl_mutex);
  do_android_update_LD_LIBRARY_PATH(ld_library_path);
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: return\n", tid, __func__);
}

static void* dlopen_ext(const char* filename, int flags, const android_dlextinfo* extinfo) {
  ScopedPthreadMutexLocker locker(&g_dl_mutex);
  soinfo* result = do_dlopen(filename, flags, extinfo);
  if (result == nullptr) {
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: %s return with error\n", tid,  __func__, filename);
    __bionic_format_dlerror("dlopen failed", linker_get_error_buffer());
    return nullptr;
  }
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: %s return\n", tid, __func__, filename);
  return result;
}

void* android_dlopen_ext(const char* filename, int flags, const android_dlextinfo* extinfo) {
  return dlopen_ext(filename, flags, extinfo);
}

void* dlopen(const char* filename, int flags) {
  return dlopen_ext(filename, flags, nullptr);
}

void* dlsym(void* handle, const char* symbol) {
  ScopedPthreadMutexLocker locker(&g_dl_mutex);

#if !defined(__LP64__)
  if (handle == nullptr) {
    __bionic_format_dlerror("dlsym library handle is null", nullptr);
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: %s[%p] return %d\n", tid, __func__, symbol, handle, __LINE__);
    return nullptr;
  }
#endif

  if (symbol == nullptr) {
    __bionic_format_dlerror("dlsym symbol name is null", nullptr);
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: %s[%p] return %d\n", tid, __func__, symbol, handle, __LINE__);
    return nullptr;
  }

  soinfo* found = nullptr;
  ElfW(Sym)* sym = nullptr;
  if (handle == RTLD_DEFAULT) {
    sym = dlsym_linear_lookup(symbol, &found, nullptr);
  } else if (handle == RTLD_NEXT) {
    void* caller_addr = __builtin_return_address(0);
    soinfo* si = find_containing_library(caller_addr);

    sym = nullptr;
    if (si && si->next) {
      sym = dlsym_linear_lookup(symbol, &found, si->next);
    }
  } else {
    sym = dlsym_handle_lookup(reinterpret_cast<soinfo*>(handle), &found, symbol);
  }

  if (sym != nullptr) {
    unsigned bind = ELF_ST_BIND(sym->st_info);

    if ((bind == STB_GLOBAL || bind == STB_WEAK) && sym->st_shndx != 0) {
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: %s[%p] return %d\n", tid, __func__, symbol, handle, __LINE__);
      return reinterpret_cast<void*>(found->resolve_symbol_address(sym));
    }

    __bionic_format_dlerror("symbol found but not global", symbol);
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: %s[%p] return %d\n", tid, __func__, symbol, handle, __LINE__);
    return nullptr;
  } else {
    __bionic_format_dlerror("undefined symbol", symbol);
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: %s[%p] return %d\n", tid, __func__, symbol, handle, __LINE__);
    return nullptr;
  }
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: %s[%p] return\n", tid, __func__, symbol, handle);
}

int dladdr(const void* addr, Dl_info* info) {
	int tid = gettid();
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: enter\n", tid, __func__);
  ScopedPthreadMutexLocker locker(&g_dl_mutex);

  // Determine if this address can be found in any library currently mapped.
  soinfo* si = find_containing_library(addr);
  if (si == nullptr) {
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: return %d\n", tid, __func__, __LINE__);
    return 0;
  }

  memset(info, 0, sizeof(Dl_info));

  info->dli_fname = si->name;
  // Address at which the shared object is loaded.
  info->dli_fbase = reinterpret_cast<void*>(si->base);

  // Determine if any symbol in the library contains the specified address.
  ElfW(Sym)* sym = dladdr_find_symbol(si, addr);
  if (sym != nullptr) {
    info->dli_sname = si->get_string(sym->st_name);
    info->dli_saddr = reinterpret_cast<void*>(si->resolve_symbol_address(sym));
  }

	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: return %d\n", tid, __func__, __LINE__);
  return 1;
}

int dlclose(void* handle) {
  ScopedPthreadMutexLocker locker(&g_dl_mutex);
  do_dlclose(reinterpret_cast<soinfo*>(handle));
  // dlclose has no defined errors.
	__libc_format_log(ANDROID_LOG_FATAL, "linker", "[%d]tw; %s: return %d\n", tid, __func__, __LINE__);
  return 0;
}

// name_offset: starting index of the name in libdl_info.strtab
#define ELF32_SYM_INITIALIZER(name_offset, value, shndx) \
    { name_offset, \
      reinterpret_cast<Elf32_Addr>(value), \
      /* st_size */ 0, \
      (shndx == 0) ? 0 : (STB_GLOBAL << 4), \
      /* st_other */ 0, \
      shndx, \
    }

#define ELF64_SYM_INITIALIZER(name_offset, value, shndx) \
    { name_offset, \
      (shndx == 0) ? 0 : (STB_GLOBAL << 4), \
      /* st_other */ 0, \
      shndx, \
      reinterpret_cast<Elf64_Addr>(value), \
      /* st_size */ 0, \
    }

#if defined(__arm__)
  // 0000000 00011111 111112 22222222 2333333 3333444444444455555555556666666 6667777777777888888888899999 9999900000000001 1111111112222222222 333333333344444444445
  // 0123456 78901234 567890 12345678 9012345 6789012345678901234567890123456 7890123456789012345678901234 5678901234567890 1234567890123456789 012345678901234567890
#  define ANDROID_LIBDL_STRTAB \
    "dlopen\0dlclose\0dlsym\0dlerror\0dladdr\0android_update_LD_LIBRARY_PATH\0android_get_LD_LIBRARY_PATH\0dl_iterate_phdr\0android_dlopen_ext\0dl_unwind_find_exidx\0"
#elif defined(__aarch64__) || defined(__i386__) || defined(__mips__) || defined(__x86_64__)
  // 0000000 00011111 111112 22222222 2333333 3333444444444455555555556666666 6667777777777888888888899999 9999900000000001 1111111112222222222
  // 0123456 78901234 567890 12345678 9012345 6789012345678901234567890123456 7890123456789012345678901234 5678901234567890 1234567890123456789
#  define ANDROID_LIBDL_STRTAB \
    "dlopen\0dlclose\0dlsym\0dlerror\0dladdr\0android_update_LD_LIBRARY_PATH\0android_get_LD_LIBRARY_PATH\0dl_iterate_phdr\0android_dlopen_ext\0"
#else
#  error Unsupported architecture. Only arm, arm64, mips, mips64, x86 and x86_64 are presently supported.
#endif

static ElfW(Sym) g_libdl_symtab[] = {
  // Total length of libdl_info.strtab, including trailing 0.
  // This is actually the STH_UNDEF entry. Technically, it's
  // supposed to have st_name == 0, but instead, it points to an index
  // in the strtab with a \0 to make iterating through the symtab easier.
  ELFW(SYM_INITIALIZER)(sizeof(ANDROID_LIBDL_STRTAB) - 1, nullptr, 0),
  ELFW(SYM_INITIALIZER)(  0, &dlopen, 1),
  ELFW(SYM_INITIALIZER)(  7, &dlclose, 1),
  ELFW(SYM_INITIALIZER)( 15, &dlsym, 1),
  ELFW(SYM_INITIALIZER)( 21, &dlerror, 1),
  ELFW(SYM_INITIALIZER)( 29, &dladdr, 1),
  ELFW(SYM_INITIALIZER)( 36, &android_update_LD_LIBRARY_PATH, 1),
  ELFW(SYM_INITIALIZER)( 67, &android_get_LD_LIBRARY_PATH, 1),
  ELFW(SYM_INITIALIZER)( 95, &dl_iterate_phdr, 1),
  ELFW(SYM_INITIALIZER)(111, &android_dlopen_ext, 1),
#if defined(__arm__)
  ELFW(SYM_INITIALIZER)(130, &dl_unwind_find_exidx, 1),
#endif
};

// Fake out a hash table with a single bucket.
//
// A search of the hash table will look through g_libdl_symtab starting with index 1, then
// use g_libdl_chains to find the next index to look at. g_libdl_chains should be set up to
// walk through every element in g_libdl_symtab, and then end with 0 (sentinel value).
//
// That is, g_libdl_chains should look like { 0, 2, 3, ... N, 0 } where N is the number
// of actual symbols, or nelems(g_libdl_symtab)-1 (since the first element of g_libdl_symtab is not
// a real symbol). (See soinfo_elf_lookup().)
//
// Note that adding any new symbols here requires stubbing them out in libdl.
static unsigned g_libdl_buckets[1] = { 1 };
#if defined(__arm__)
static unsigned g_libdl_chains[] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0 };
#else
static unsigned g_libdl_chains[] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
#endif

static soinfo __libdl_info("libdl.so", nullptr, 0);

// This is used by the dynamic linker. Every process gets these symbols for free.
soinfo* get_libdl_info() {
  if ((__libdl_info.flags & FLAG_LINKED) == 0) {
    __libdl_info.flags |= FLAG_LINKED;
    __libdl_info.strtab = ANDROID_LIBDL_STRTAB;
    __libdl_info.symtab = g_libdl_symtab;
    __libdl_info.nbucket = sizeof(g_libdl_buckets)/sizeof(unsigned);
    __libdl_info.nchain = sizeof(g_libdl_chains)/sizeof(unsigned);
    __libdl_info.bucket = g_libdl_buckets;
    __libdl_info.chain = g_libdl_chains;
    __libdl_info.ref_count = 1;
    __libdl_info.strtab_size = sizeof(ANDROID_LIBDL_STRTAB);
  }

  return &__libdl_info;
}
