#ifndef MSTATX_H
#define MSTATX_H
#ifdef __linux__
#	include <linux/version.h>
#else
#	error "This program uses statx which is Linux-only"
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
#	error "This program uses statx which is Linux >v4.11.0-only"
#endif
#include <linux/types.h>
#include <unistd.h>
#include <sys/syscall.h>           /* For SYS_xxx definitions */

struct statx_timestamp {
	__s64 tv_sec;
	__u32 tv_nsec;
	__s32 __reserved;
};
typedef struct statx_timestamp statxTm_t;

struct statx {
	__u32 stx_mask;            /* What results were written [uncond] */
	__u32 stx_blksize;         /* Preferred general I/O size [uncond] */
	__u64 stx_attributes;      /* Flags conveying info about file [uncond]*/
	__u32 stx_nlink;           /* Number of hard links */
	__u32 stx_uid;             /* User ID of owner */
	__u32 stx_gid;             /* Group ID of owner */
	__u16 stx_mode;            /* File mode */
	__u16 __spare0[1];         
	__u64 stx_ino;             /* Inode number */
	__u64 stx_size;            /* File size */
	__u64 stx_blocks;          /* Number of 512-byte blocks allocated */
	__u64 stx_attributes_mask; /* Shows what stx_attributes supports */
	statxTm_t stx_atime;       /* Last access time */
	statxTm_t stx_btime;       /* File creation time */
	statxTm_t stx_ctime;       /* Last attribute change time */
	statxTm_t stx_mtime;       /* Last data mod time */
	__u32 stx_rdev_major;      /* Device ID of special file [if bdev/cdev]*/
	__u32 stx_rdev_minor;
	__u32 stx_dev_major;       /* ID of device containing file [uncond] */
	__u32 stx_dev_minor;
	__u64 __spare2[14];        /* Spare space for future expansion */
};
typedef struct statx statx_t;

#define STATX_TYPE             0x00000001U /* Want/got stx_mode & S_IFMT */
#define STATX_MODE             0x00000002U /* Want/got stx_mode & ~S_IFMT */
#define STATX_NLINK            0x00000004U /* Want/got stx_nlink */
#define STATX_UID              0x00000008U /* Want/got stx_uid */
#define STATX_GID              0x00000010U /* Want/got stx_gid */
#define STATX_ATIME            0x00000020U /* Want/got stx_atime */
#define STATX_MTIME            0x00000040U /* Want/got stx_mtime */
#define STATX_CTIME            0x00000080U /* Want/got stx_ctime */
#define STATX_INO              0x00000100U /* Want/got stx_ino */
#define STATX_SIZE             0x00000200U /* Want/got stx_size */
#define STATX_BLOCKS           0x00000400U /* Want/got stx_blocks */
#define STATX_BASIC_STATS      0x000007ffU /* stuff in normal struct stat */
#define STATX_BTIME            0x00000800U /* Want/got stx_btime */
#define STATX_ALL              0x00000fffU /* All currently supported flags */
#define STATX__RESERVED        0x80000000U /* Reserved for future expansion */

#define STATX_ATTR_COMPRESSED  0x00000004 /*[I] File compressed by the fs */
#define STATX_ATTR_IMMUTABLE   0x00000010 /*[I] File marked immutable */
#define STATX_ATTR_APPEND      0x00000020 /*[I] File append-only */
#define STATX_ATTR_NODUMP      0x00000040 /*[I] File not to be dumped */
#define STATX_ATTR_ENCRYPTED   0x00000800 /*[I] File requires decryption key */
#define STATX_ATTR_AUTOMOUNT   0x00001000 /*Dir: Automount trigger */
#define STATX_ATTR_VERITY      0x00100000 /*[I] Verity protected file */

#ifndef AT_FDCWD
#	define AT_FDCWD              -100
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#	define AT_SYMLINK_NOFOLLOW   0x100
#endif
#ifndef AT_REMOVEDIR
#	define AT_REMOVEDIR          0x200
#endif
#ifndef AT_SYMLINK_FOLLOW
#	define AT_SYMLINK_FOLLOW     0x400
#endif
#ifndef AT_NO_AUTOMOUNT
#	define AT_NO_AUTOMOUNT       0x800
#endif
#ifndef AT_EMPTY_PATH
#	define AT_EMPTY_PATH         0x100
#endif
#ifndef AT_STATX_SYNC_TYPE
#	define AT_STATX_SYNC_TYPE    0x6000
#endif
#ifndef AT_STATX_SYNC_AS_STAT
#	define AT_STATX_SYNC_AS_STAT 0x0000
#endif
#ifndef AT_STATX_FORCE_SYNC
#	define AT_STATX_FORCE_SYNC   0x2000
#endif
#ifndef AT_STATX_DONT_SYNC
#	define AT_STATX_DONT_SYNC    0x4000
#endif
#ifndef AT_RECURSIVE
#	define AT_RECURSIVE          0x8000
#endif
#ifndef AT_EACCESS
#	define AT_EACCESS            0x200
#endif

static inline int statx(int dirfd, const char *pathname, int flags,
                        unsigned int mask, statx_t *stx)
{
	return syscall(__NR_statx, dirfd, pathname, flags, mask, stx);
}
#endif /* MSTATX_H */
