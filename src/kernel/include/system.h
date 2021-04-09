#pragma once

#ifndef ASSEMBLY

#include <err.h>
#include <printk.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define likely(x) __builtin_expect(x, 1)
#define unlikely(x) __builtin_expect(x, 0)

typedef long ssize_t;
typedef unsigned __int128 uint128_t;

/*
typedef uint128_t objid_t;

#define IDFMT "%lx:%lx"

#define IDPR(id) \
    (uint64_t)((id) >> 64), (uint64_t)(id & 0xFFFFFFFFFFFFFFFF)
*/
static inline unsigned long long __round_up_pow2(unsigned int a)
{
	return ((a & (a - 1)) == 0) ? a : 1ull << (sizeof(a) * 8 - __builtin_clz(a));
}

#define flag_if_notzero(f, x) ({ (f) ? x : 0; })

#define align_down(x, s) ({ (x) & ~(s - 1); })

#define align_up(x, s)                                                                             \
	({                                                                                             \
		typeof(x) __y = (x);                                                                       \
		size_t __sz = (s);                                                                         \
		((__y - 1) & ~(__sz - 1)) + __sz;                                                          \
	})

#define __orderedbefore(x) (x - 1)
#define __orderedafter(x) (x + 1)

#define __initializer __attribute__((used, constructor, no_instrument_function))
#define __orderedinitializer(x) __attribute__((used, constructor(x + 3000), no_instrument_function))

#define __cleanup(f) __attribute__((cleanup(f)))
#define ___concat(x, y) x##y
#define __concat(x, y) ___concat(x, y)

#define __get_macro2(_1, _2, NAME, ...) NAME

#define stringify_define(x) stringify(x)
#define stringify(x) #x

#define array_len(x) (sizeof((x)) / sizeof((x)[0]))

#define container_of(ptr, type, member)                                                            \
	({                                                                                             \
		const typeof(((type *)0)->member) *__mptr = (ptr);                                         \
		(type *)((char *)__mptr - offsetof(type, member));                                         \
	})

#define __unused __attribute__((unused))
#define __packed __attribute__((packed))

#define __noinstrument __attribute__((no_instrument_function))

long strtol(char *str, char **end, int base);
#endif
