/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_ATOMIC64_H
#define __UM_LIBMODE_ATOMIC64_H

#include <linux/types.h>

#define ATOMIC64_OP(op, c_op)					\
	static inline void arch_atomic64_##op(s64 i, atomic64_t *v)	\
	{							\
		__atomic_fetch_##op(&v->counter, i, __ATOMIC_RELAXED);	\
	}

#define ATOMIC64_OP_RETURN(op, c_op)					\
	static inline s64 arch_atomic64_##op##_return(s64 i, atomic64_t *v)	\
	{								\
		return __atomic_##op##_fetch(&v->counter, i, __ATOMIC_RELAXED); \
	}

#define ATOMIC64_FETCH_OP(op, c_op)					\
	static inline s64 arch_atomic64_fetch_##op(s64 i, atomic64_t *v)	\
	{								\
		return __atomic_fetch_##op(&v->counter, i, __ATOMIC_RELAXED); \
	}

#ifndef arch_atomic64_add_return
ATOMIC64_OP_RETURN(add, +)
#endif

#ifndef arch_atomic64_sub_return
	ATOMIC64_OP_RETURN(sub, -)
#endif

#ifndef arch_atomic64_fetch_add
	ATOMIC64_FETCH_OP(add, +)
#endif

#ifndef arch_atomic64_fetch_sub
	ATOMIC64_FETCH_OP(sub, -)
#endif

#ifndef arch_atomic64_fetch_and
	ATOMIC64_FETCH_OP(and, &)
#endif

#ifndef arch_atomic64_fetch_or
	ATOMIC64_FETCH_OP(or, |)
#endif

#ifndef arch_atomic64_fetch_xor
	ATOMIC64_FETCH_OP(xor, ^)
#endif

#ifndef arch_atomic64_and
	ATOMIC64_OP(and, &)
#endif

#ifndef arch_atomic64_or
	ATOMIC64_OP(or, |)
#endif

#ifndef arch_atomic64_xor
	ATOMIC64_OP(xor, ^)
#endif

#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP


#define ATOMIC64_INIT(i)       { (i) }

static inline void arch_atomic64_add(s64 i, atomic64_t *v)
{
	arch_atomic64_add_return(i, v);
}

static inline void arch_atomic64_sub(s64 i, atomic64_t *v)
{
	arch_atomic64_sub_return(i, v);
}

#ifndef arch_atomic64_read
#define arch_atomic64_read(v)       READ_ONCE((v)->counter)
#endif

#define arch_atomic64_set(v, i) WRITE_ONCE(((v)->counter), (i))

#define arch_atomic64_xchg(ptr, v)          (arch_xchg(&(ptr)->counter, (v)))
#define arch_atomic64_cmpxchg(v, old, new)  (arch_cmpxchg(&((v)->counter), (old), (new)))

#endif /* __LKL_ATOMIC64_H */
