/* Linux batch syscall tester: statx all files specified in argv[]
   with one batch system call. */
#include <time.h>
#include <stdio.h>       /* printf */
#include <sys/types.h>
#include "mstatx.h"      /* statx interface */
#include "linux/batch.h" /* batching interface */

int main(int ac, char **av) {
	long            rvs[ac-1], dt, i, msk = STATX_TYPE,
	                flg = AT_SYMLINK_NOFOLLOW|AT_NO_AUTOMOUNT;
	syscall_t       bat[ac-1];
	statx_t         s  [ac-1];
	struct timespec t0, t1;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (ac--, av++, i = 0; i < ac; i++)
		bat[i] = scall5(statx, 0,0,0, AT_FDCWD, av[i], flg, msk, &s[i]);
	batch(rvs, bat, ac, 0, 0);
#ifdef TEST
	for (i = 0; i < ac; i++)
		printf("size(%s)=%llu\n", av[i], s[i].stx_size);
#endif
	clock_gettime(CLOCK_MONOTONIC, &t1);
	dt = (t1.tv_sec - t0.tv_sec)*1000000000 + (t1.tv_nsec - t0.tv_nsec);
	printf("%ld ns\n", dt);
	return 0;
}
