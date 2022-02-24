// SPDX-License-Identifier: GPL-2.0
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <lkl_host.h>

char lkl_um_devs[4096];

static const char * const lkl_err_strings[] = {
	[0]			= "Success",
	[LKL_EPERM]		= "Operation not permitted",
	[LKL_ENOENT]		= "No such file or directory",
	[LKL_ESRCH]		= "No such process",
	[LKL_EINTR]		= "Interrupted system call",
	[LKL_EIO]		= "I/O error",
	[LKL_ENXIO]		= "No such device or address",
	[LKL_E2BIG]		= "Argument list too long",
	[LKL_ENOEXEC]		= "Exec format error",
	[LKL_EBADF]		= "Bad file number",
	[LKL_ECHILD]		= "No child processes",
	[LKL_EAGAIN]		= "Try again",
	[LKL_ENOMEM]		= "Out of memory",
	[LKL_EACCES]		= "Permission denied",
	[LKL_EFAULT]		= "Bad address",
	[LKL_ENOTBLK]		= "Block device required",
	[LKL_EBUSY]		= "Device or resource busy",
	[LKL_EEXIST]		= "File exists",
	[LKL_EXDEV]		= "Cross-device link",
	[LKL_ENODEV]		= "No such device",
	[LKL_ENOTDIR]		= "Not a directory",
	[LKL_EISDIR]		= "Is a directory",
	[LKL_EINVAL]		= "Invalid argument",
	[LKL_ENFILE]		= "File table overflow",
	[LKL_EMFILE]		= "Too many open files",
	[LKL_ENOTTY]		= "Not a typewriter",
	[LKL_ETXTBSY]		= "Text file busy",
	[LKL_EFBIG]		= "File too large",
	[LKL_ENOSPC]		= "No space left on device",
	[LKL_ESPIPE]		= "Illegal seek",
	[LKL_EROFS]		= "Read-only file system",
	[LKL_EMLINK]		= "Too many links",
	[LKL_EPIPE]		= "Broken pipe",
	[LKL_EDOM]		= "Math argument out of domain of func",
	[LKL_ERANGE]		= "Math result not representable",
	[LKL_EDEADLK]		= "Resource deadlock would occur",
	[LKL_ENAMETOOLONG]	= "File name too long",
	[LKL_ENOLCK]		= "No record locks available",
	[LKL_ENOSYS]		= "Invalid system call number",
	[LKL_ENOTEMPTY]		= "Directory not empty",
	[LKL_ELOOP]		= "Too many symbolic links encountered",
	[LKL_ENOMSG]		= "No message of desired type",
	[LKL_EIDRM]		= "Identifier removed",
	[LKL_ECHRNG]		= "Channel number out of range",
	[LKL_EL2NSYNC]		= "Level 2 not synchronized",
	[LKL_EL3HLT]		= "Level 3 halted",
	[LKL_EL3RST]		= "Level 3 reset",
	[LKL_ELNRNG]		= "Link number out of range",
	[LKL_EUNATCH]		= "Protocol driver not attached",
	[LKL_ENOCSI]		= "No CSI structure available",
	[LKL_EL2HLT]		= "Level 2 halted",
	[LKL_EBADE]		= "Invalid exchange",
	[LKL_EBADR]		= "Invalid request descriptor",
	[LKL_EXFULL]		= "Exchange full",
	[LKL_ENOANO]		= "No anode",
	[LKL_EBADRQC]		= "Invalid request code",
	[LKL_EBADSLT]		= "Invalid slot",
	[LKL_EBFONT]		= "Bad font file format",
	[LKL_ENOSTR]		= "Device not a stream",
	[LKL_ENODATA]		= "No data available",
	[LKL_ETIME]		= "Timer expired",
	[LKL_ENOSR]		= "Out of streams resources",
	[LKL_ENONET]		= "Machine is not on the network",
	[LKL_ENOPKG]		= "Package not installed",
	[LKL_EREMOTE]		= "Object is remote",
	[LKL_ENOLINK]		= "Link has been severed",
	[LKL_EADV]		= "Advertise error",
	[LKL_ESRMNT]		= "Srmount error",
	[LKL_ECOMM]		= "Communication error on send",
	[LKL_EPROTO]		= "Protocol error",
	[LKL_EMULTIHOP]		= "Multihop attempted",
	[LKL_EDOTDOT]		= "RFS specific error",
	[LKL_EBADMSG]		= "Not a data message",
	[LKL_EOVERFLOW]		= "Value too large for defined data type",
	[LKL_ENOTUNIQ]		= "Name not unique on network",
	[LKL_EBADFD]		= "File descriptor in bad state",
	[LKL_EREMCHG]		= "Remote address changed",
	[LKL_ELIBACC]		= "Can not access a needed shared library",
	[LKL_ELIBBAD]		= "Accessing a corrupted shared library",
	[LKL_ELIBSCN]		= ".lib section in a.out corrupted",
	[LKL_ELIBMAX]		= "Attempting to link in too many shared libraries",
	[LKL_ELIBEXEC]		= "Cannot exec a shared library directly",
	[LKL_EILSEQ]		= "Illegal byte sequence",
	[LKL_ERESTART]		= "Interrupted system call should be restarted",
	[LKL_ESTRPIPE]		= "Streams pipe error",
	[LKL_EUSERS]		= "Too many users",
	[LKL_ENOTSOCK]		= "Socket operation on non-socket",
	[LKL_EDESTADDRREQ]	= "Destination address required",
	[LKL_EMSGSIZE]		= "Message too long",
	[LKL_EPROTOTYPE]	= "Protocol wrong type for socket",
	[LKL_ENOPROTOOPT]	= "Protocol not available",
	[LKL_EPROTONOSUPPORT]	= "Protocol not supported",
	[LKL_ESOCKTNOSUPPORT]	= "Socket type not supported",
	[LKL_EOPNOTSUPP]	= "Operation not supported on transport endpoint",
	[LKL_EPFNOSUPPORT]	= "Protocol family not supported",
	[LKL_EAFNOSUPPORT]	= "Address family not supported by protocol",
	[LKL_EADDRINUSE]	= "Address already in use",
	[LKL_EADDRNOTAVAIL]	= "Cannot assign requested address",
	[LKL_ENETDOWN]		= "Network is down",
	[LKL_ENETUNREACH]	= "Network is unreachable",
	[LKL_ENETRESET]		= "Network dropped connection because of reset",
	[LKL_ECONNABORTED]	= "Software caused connection abort",
	[LKL_ECONNRESET]	= "Connection reset by peer",
	[LKL_ENOBUFS]		= "No buffer space available",
	[LKL_EISCONN]		= "Transport endpoint is already connected",
	[LKL_ENOTCONN]		= "Transport endpoint is not connected",
	[LKL_ESHUTDOWN]		= "Cannot send after transport endpoint shutdown",
	[LKL_ETOOMANYREFS]	= "Too many references: cannot splice",
	[LKL_ETIMEDOUT]		= "Connection timed out",
	[LKL_ECONNREFUSED]	= "Connection refused",
	[LKL_EHOSTDOWN]		= "Host is down",
	[LKL_EHOSTUNREACH]	= "No route to host",
	[LKL_EALREADY]		= "Operation already in progress",
	[LKL_EINPROGRESS]	= "Operation now in progress",
	[LKL_ESTALE]		= "Stale file handle",
	[LKL_EUCLEAN]		= "Structure needs cleaning",
	[LKL_ENOTNAM]		= "Not a XENIX named type file",
	[LKL_ENAVAIL]		= "No XENIX semaphores available",
	[LKL_EISNAM]		= "Is a named type file",
	[LKL_EREMOTEIO]		= "Remote I/O error",
	[LKL_EDQUOT]		= "Quota exceeded",
	[LKL_ENOMEDIUM]		= "No medium found",
	[LKL_EMEDIUMTYPE]	= "Wrong medium type",
	[LKL_ECANCELED]		= "Operation Canceled",
	[LKL_ENOKEY]		= "Required key not available",
	[LKL_EKEYEXPIRED]	= "Key has expired",
	[LKL_EKEYREVOKED]	= "Key has been revoked",
	[LKL_EKEYREJECTED]	= "Key was rejected by service",
	[LKL_EOWNERDEAD]	= "Owner died",
	[LKL_ENOTRECOVERABLE]	= "State not recoverable",
	[LKL_ERFKILL]		= "Operation not possible due to RF-kill",
	[LKL_EHWPOISON]		= "Memory page has hardware error",
};

const char *lkl_strerror(int err)
{
	if (err < 0)
		err = -err;

	if ((size_t)err >= sizeof(lkl_err_strings) / sizeof(const char *))
		return "Bad error code";

	return lkl_err_strings[err];
}

void lkl_perror(char *msg, int err)
{
	const char *err_msg = lkl_strerror(err);
	/* We need to use 'real' printf because lkl_print can
	 * be turned off when debugging is off.
	 */
	lkl_printf("%s: %s\n", msg, err_msg);
}

static int lkl_vprintf(const char *fmt, va_list args)
{
	int n;
	char *buffer;
	va_list copy;

	va_copy(copy, args);
	n = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);

	buffer = lkl_mem_alloc(n + 1);
	if (!buffer)
		return (-1);

	vsnprintf(buffer, n + 1, fmt, args);

	lkl_print(buffer, n);
	lkl_mem_free(buffer);

	return n;
}

int lkl_printf(const char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	n = lkl_vprintf(fmt, args);
	va_end(args);

	return n;
}

void lkl_bug(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	lkl_vprintf(fmt, args);
	va_end(args);

	lkl_panic();
}

/* XXX */
typedef unsigned int u32;
#define __user
int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	return 0;
}
int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr)
{
	return 0;
}
