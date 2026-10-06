/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * What the C++ library would throw is fatal here: the image is built without exceptions and
 * without the exception runtime of libstdc++.
 */
#include <bits/functexcept.h>
#include <cstdlib>

extern "C" void printk(const char *fmt, ...);

namespace std {

[[noreturn]] static void fatal(const char *what)
{
	printk("litehtml: %s\n", what);
	abort();
}

void __throw_bad_alloc() { fatal("bad_alloc"); }
void __throw_bad_array_new_length() { fatal("bad_array_new_length"); }
void __throw_bad_cast() { fatal("bad_cast"); }
void __throw_bad_typeid() { fatal("bad_typeid"); }
void __throw_logic_error(const char *s) { fatal(s); }
void __throw_domain_error(const char *s) { fatal(s); }
void __throw_invalid_argument(const char *s) { fatal(s); }
void __throw_length_error(const char *s) { fatal(s); }
void __throw_out_of_range(const char *s) { fatal(s); }
void __throw_out_of_range_fmt(const char *s, ...) { fatal(s); }
void __throw_runtime_error(const char *s) { fatal(s); }
void __throw_range_error(const char *s) { fatal(s); }
void __throw_overflow_error(const char *s) { fatal(s); }
void __throw_underflow_error(const char *s) { fatal(s); }
void __throw_bad_function_call() { fatal("bad_function_call"); }
void __throw_bad_weak_ptr() { fatal("bad_weak_ptr"); }
void __throw_system_error(int) { fatal("system_error"); }
void __throw_future_error(int) { fatal("future_error"); }
void __throw_ios_failure(const char *s) { fatal(s); }
void __throw_ios_failure(const char *s, int) { fatal(s); }

} // namespace std

/*
 * Guards of function local statics (single threaded: the page is built by one thread). The
 * ones of libstdc++ throw on a recursive initialisation and so need the exception runtime.
 */
extern "C" {

int __cxa_guard_acquire(long long *guard)
{
	return *(char *)guard == 0;
}

void __cxa_guard_release(long long *guard)
{
	*(char *)guard = 1;
}

void __cxa_guard_abort(long long *)
{
}

} // extern "C"
