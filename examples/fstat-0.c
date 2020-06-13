#include <stdio.h>          // fprintf stderr
#include <stdlib.h>         // calloc
#include <time.h>           // timespec clock_gettime CLOCK_MONOTONIC
#include <unistd.h>
#include <fcntl.h>          // AT_FDCWD
#include <sys/types.h>
#include <sys/stat.h>
#include "linux/batch.h"    // syscall_t batch

int N[] = {  1,  2,   3,   4,   5,   6,    7,  8,  9, 10,
            16, 20,  24,  32,  36,  40,   44, 50, 60, 70,
            80, 90, 100, 128, 256, 384, 1024 };

int main(int ac, char **av) {
	struct stat      st;
	struct timespec  t0, t1;
	unsigned long    dt;
	int              i, j, k, m, nn = sizeof N / sizeof *N;
	syscall_t       *bat = (syscall_t *)calloc(1024, sizeof *bat);
	long            *rvs = (long *)calloc(1024, sizeof *rvs);

	for (i = 0; i < N[nn - 1]; i++) {           //Assume last biggest
		bat[i].nr     = __NR_fstat;
		bat[i].argc   = 2;
		bat[i].arg[0] = (long)0;
		bat[i].arg[1] = (long)&st;
	}
	printf("N\tm\tns\trs\n");
	for (k = 0; k < nn; k++) {
		unsigned long min = 1000000000000;  //Min out of 10 trials
		unsigned long rs = 0;
		m = 1000000 / (381 + 88*N[k]);      //Make similar real-time
		for (i = 0; i < 10; i++) {
			clock_gettime(CLOCK_MONOTONIC, &t0);
			for (j = 0; j < m; j++)
				rs += batch(rvs, bat, N[k], 0, 0);
			clock_gettime(CLOCK_MONOTONIC, &t1);
			dt = (t1.tv_sec - t0.tv_sec) * 1000000000 +
				t1.tv_nsec - t0.tv_nsec;
			if (dt < min)
				min = dt;
		}
		printf("%d\t%d\t%ld\t%lu\n", N[k], m, min, rs);
	}	// NOTE: adjust for user-space loop & gettime overheads
	return 0;
} // i7-6700k@4.7GHz ns =~ 381*m + 88*N*m = (381+88*N)*m
