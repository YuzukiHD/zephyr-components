/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "quickjs-libc.h"
#include "dtoa.h"

void js_std_init_handlers(JSRuntime *rt)
{
	(void)rt;
}

void js_std_free_handlers(JSRuntime *rt)
{
	(void)rt;
}

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val;
	for (int i = 0; i < argc; i++) {
		const char *s = JS_ToCString(ctx, argv[i]);

		if (s == NULL) {
			return JS_EXCEPTION;
		}
		fputs(i != 0 ? " " : "", stdout);
		fputs(s, stdout);
		JS_FreeCString(ctx, s);
	}
	fputc('\n', stdout);
	return JS_UNDEFINED;
}

void js_std_add_helpers(JSContext *ctx, int argc, char **argv)
{
	(void)argc;
	(void)argv;
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue console = JS_NewObject(ctx);

	JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_print, "log", 1));
	JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, js_print, "error", 1));
	JS_SetPropertyStr(ctx, console, "warn", JS_NewCFunction(ctx, js_print, "warn", 1));
	JS_SetPropertyStr(ctx, global, "console", console);
	JS_SetPropertyStr(ctx, global, "print", JS_NewCFunction(ctx, js_print, "print", 1));
	JS_FreeValue(ctx, global);
}

void js_std_dump_error(JSContext *ctx)
{
	JSValue exc = JS_GetException(ctx);
	const char *msg = JS_ToCString(ctx, exc);

	printf("[E] js: %s\n", msg != NULL ? msg : "<exception>");
	JS_FreeCString(ctx, msg);
	if (JS_IsError(exc)) {
		JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");

		if (!JS_IsUndefined(stack)) {
			const char *st = JS_ToCString(ctx, stack);

			if (st != NULL) {
				printf("%s\n", st);
				JS_FreeCString(ctx, st);
			}
		}
		JS_FreeValue(ctx, stack);
	}
	JS_FreeValue(ctx, exc);
}

/* --- what the minimal C library does not have ------------------------------ */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <sys/time.h>

char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);

	if (p != NULL) {
		memcpy(p, s, n);
	}
	return p;
}

/* wall clock: there is no RTC, the time since boot is used */
int gettimeofday(struct timeval *tv, void *tz)
{
	int64_t us = k_ticks_to_us_floor64(k_uptime_ticks());

	(void)tz;
	tv->tv_sec = us / 1000000;
	tv->tv_usec = us % 1000000;
	return 0;
}

/* the number parser of QuickJS, correctly rounded */
double strtod(const char *s, char **end)
{
	JSATODTempMem mem;
	const char *next = s;
	double v;

	while (*s == ' ' || (*s >= '\t' && *s <= '\r')) {
		s++;
	}
	v = js_atod(s, &next, 10, 0, &mem);
	if (end != NULL) {
		*end = (char *)(next == s ? s : next);
	}
	return v;
}

/* the errno of the toolchain's libm */
int *__errno(void)
{
	return &errno;
}
