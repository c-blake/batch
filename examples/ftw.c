#ifndef FTW_C
#define FTW_C
#include <string.h>      /**/
#include <stdlib.h>      /**/
#include <errno.h>       /**/
#include <stdio.h>       /**/
#include <fcntl.h>       /* O_RDONLY */
#include <dirent.h>      /* DT_* */
#include <unistd.h>      /**/
#include <sys/types.h>   /**/
#include <sys/stat.h>    /**/
#include "ftw.h"         /**/
#include <sys/syscall.h> /**/
#include <linux/batch.h> /**/

struct ftw_s {            // Cross-recursion state encapsulation.  This does NOT
	char  path[4096], // make the routine MT-safe as chdir() is still used.
	     *pfx;        // Concurrent ftws need expensive full path stats.
	ftw_f visit;      // Encapsulation here i sjust for hygiene.
	long  flags, mask;  // statx controls
	__u32 major, minor, maxDepth; // device containing root, max recursion depth
        char  xdev;
};

static inline int dotOrDotDot(char *dir) {
	return dir[0] == '.' && (!dir[1] || (dir[1] == '.' && !dir[2]));
}

struct dent {
	unsigned long  d_ino, d_off; 
	unsigned short d_reclen;
	char           d_type;
	char           d_name[];
};
typedef struct dent dent_t;

static inline int gdents(int fd, struct dent *buf, size_t len) {
	return syscall(__NR_getdents64, fd, buf, len);
}

#define BUFSZ 4096
#define DENT_FOREACH(de, buf, n) \
	for (de = (dent_t *) buf; (char *) de < buf + n; \
	     de = (dent_t *)((char*)de + de->d_reclen))

static void ftwR(struct ftw_s *s, int dfd, int nPath) {
	char         buf[2048];
	struct dent *de;
	statx_t     *sts;
	syscall_t   *bat;
	long        *rvs, nB = 0, i;
	int         *dts;
	s->path[nPath++] = '/';
	char *ent = s->path + nPath;
	while (1) {
		int nRd = syscall(__NR_getdents64, dfd, buf, sizeof buf);
		if (nRd == -1) { perror("getdents"); return; }
		if (nRd == 0)
			break;
		nB  = 0;
		DENT_FOREACH(de, buf, nRd)
			if (dotOrDotDot(de->d_name)) continue; else nB++;
		dts = (int       *)alloca(nB * sizeof *dts);
		rvs = (long      *)alloca(nB * sizeof *rvs);
		bat = (syscall_t *)alloca(nB * sizeof *bat);
		sts = (statx_t   *)alloca(nB * sizeof *sts);
		i = 0;
		DENT_FOREACH(de, buf, nRd) {
			if (dotOrDotDot(de->d_name)) continue;
			dts[i] = de->d_type;
			sts[i].stx_dev_major = sts[i].stx_nlink = 0;
			bat[i] = scall5(statx, 0,0,0, dfd, de->d_name,
			                s->flags, s->mask, &sts[i]);
			i++;
		}
		batch(&rvs[0], &bat[0], nB, 0, 0);
		for (i = 0; i < nB; i++) {
			char *name = (char *)(bat[i].arg[1]);
			int m = strlen(name);
			memcpy(ent, name, m + 1);
			s->visit(s->path, s->path + nPath, sts + i);
			if (dts[i] == DT_DIR && (!s->xdev ||
			    (sts[i].stx_dev_major == s->major &&
			     sts[i].stx_dev_minor == s->minor))) {
				int fd = openat(dfd, s->path + nPath,
				                O_RDONLY|O_DIRECTORY);
				if (fd == -1) { perror("open"); return; }
				ftwR(s, fd, nPath + m);
				if (close(fd)) { perror("close"); return; }
			}
		}
	}
}

void ftw(const char *path, ftw_f visit, int maxDepth, int xdev) {
	struct ftw_s s = { "", NULL, visit,
	                   AT_SYMLINK_NOFOLLOW|AT_NO_AUTOMOUNT, STATX_TYPE };
	statx_t root;
	int     dfd;
	root.stx_dev_major = root.stx_nlink = 0;
	if (statx(AT_FDCWD, path, s.flags, s.mask, &root) < 0) {
		perror("statx");
		return;
	}
	if (S_ISDIR(root.stx_mode)) {
		int m = strlen(path);
		memcpy(s.path, path, m + 1);
		s.pfx = s.path;
		s.major = root.stx_dev_major;
		s.minor = root.stx_dev_minor;
		s.maxDepth = maxDepth;
		s.xdev = !!xdev;
		if ((dfd = open(s.path, O_RDONLY|O_DIRECTORY)) < 0)
			perror("statx");
		else
			ftwR(&s, dfd, m);
	}
	visit(path, path, &root);
}
#endif //FTW_C
