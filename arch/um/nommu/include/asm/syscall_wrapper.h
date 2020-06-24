/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __UM_SYSCALL_WRAPPER_H
#define __UM_SYSCALL_WRAPPER_H

#define __SC_ASCII(t, a) #t "," #a

#define __ASCII_MAP0(m, ...)
#define __ASCII_MAP1(m, t, a) m(t, a)
#define __ASCII_MAP2(m, t, a, ...) m(t, a) "," __ASCII_MAP1(m, __VA_ARGS__)
#define __ASCII_MAP3(m, t, a, ...) m(t, a) "," __ASCII_MAP2(m, __VA_ARGS__)
#define __ASCII_MAP4(m, t, a, ...) m(t, a) "," __ASCII_MAP3(m, __VA_ARGS__)
#define __ASCII_MAP5(m, t, a, ...) m(t, a) "," __ASCII_MAP4(m, __VA_ARGS__)
#define __ASCII_MAP6(m, t, a, ...) m(t, a) "," __ASCII_MAP5(m, __VA_ARGS__)
#define __ASCII_MAP(n, ...) __ASCII_MAP##n(__VA_ARGS__)

#ifdef __MINGW32__
#define SECTION_ATTRS "n0"
#else
#define SECTION_ATTRS "a"
#endif

#define __SYSCALL_DEFINE_ARCH(x, name, ...)				\
	asm(".section .syscall_defs,\"" SECTION_ATTRS "\"\n"		\
	    ".ascii \"#ifdef __NR" #name "\\n\"\n"			\
	    ".ascii \"SYSCALL_DEFINE" #x "(" #name ","			\
	    __ASCII_MAP(x, __SC_ASCII, __VA_ARGS__) ")\\n\"\n"		\
	    ".ascii \"#endif\\n\"\n"					\
	    ".section .text\n")

#define SYSCALL_DEFINE0(sname)					\
	SYSCALL_METADATA(_##sname, 0);				\
	__SYSCALL_DEFINE_ARCH(0, _##sname);			\
	asmlinkage long sys_##sname(void);			\
	ALLOW_ERROR_INJECTION(sys_##sname, ERRNO);		\
	asmlinkage long sys_##sname(void)

#define __SYSCALL_DEFINEx(x, name, ...)					\
	__SYSCALL_DEFINE_ARCH(x, name, __VA_ARGS__);			\
	__diag_push();							\
	__diag_ignore(GCC, 8, "-Wattribute-alias",			\
		      "Type aliasing is used to sanitize syscall arguments");\
	asmlinkage long sys##name(__MAP(x, __SC_DECL, __VA_ARGS__))	\
		__attribute__((alias(__stringify(__se_sys##name))));	\
	ALLOW_ERROR_INJECTION(sys##name, ERRNO);			\
	static inline long __do_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__)); \
	asmlinkage long __se_sys##name(__MAP(x, __SC_LONG, __VA_ARGS__)); \
	asmlinkage long __se_sys##name(__MAP(x, __SC_LONG, __VA_ARGS__)) \
	{								\
		long ret = __do_sys##name(__MAP(x, __SC_CAST, __VA_ARGS__)); \
		__MAP(x, __SC_TEST, __VA_ARGS__);			\
		__PROTECT(x, ret, __MAP(x, __SC_ARGS, __VA_ARGS__));	\
		return ret;						\
	}								\
	__diag_pop();							\
	static inline long __do_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__))

#endif /* __UM_SYSCALL_WRAPPER_H */
