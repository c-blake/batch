/* Linux batch syscall tester: stat all files specified in argv[]
   with one batch system call.*/
#include <stdio.h>          /* printf */
#include <sys/types.h>
#include <sys/stat.h>       /* *KERNEL*'s idea of struct stat (GRRR) */
#include "linux/batch.h"    /* batching interface */
#include <time.h>

int main(int ac, char **av) {
	int             i;
	long            rvs[ac-1], dt;
	syscall_t       bat[ac-1];
	struct stat     st [ac-1];
	struct timespec t0, t1;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (ac--, av++, i = 0; i < ac; i++)
		bat[i] = (syscall_t){ __NR_lstat, 0, 0, 0, 2,
		                      (long)(av[i]), (long)&st[i] };
	batch(rvs, bat, ac, 0, 0);
#ifdef TEST
	for (i = 0; i < ac; i++)
		printf("size(%s)=%ld\n", av[i], st[i].st_size);
#endif
	clock_gettime(CLOCK_MONOTONIC, &t1);
	dt = (t1.tv_sec - t0.tv_sec)*1000000000 + (t1.tv_nsec - t0.tv_nsec);
	printf("%ld ns\n", dt);
	return 0;
}
