#include <fcntl.h>       // O_ macros
#include <stdio.h>       // fprintf, stderr
#include <stdlib.h>      // malloc, free
#include <string.h>      // strerror()
#include <time.h>        // struct timespec, clock_* CLOCK_*
#include "linux/batch.h" // syscall_t, batch*|BATCH* macros

// Populate `c` with a `3*n` syscall program referring to `r` that does read-
// write cycles until either `read()<=0`, `write()<=0`, or batch is exhausted.
// Using the Linux splice system instead should be quite a bit faster.

void mkRW(syscall_t *c, long *r, int n, int fI, int fO, char *buf, size_t len) {
	for (int i = 0; i < n; i++) {
		c[3*i+0] = scall3(read,  -1, -1, 0, fI, buf, len);
		c[3*i+1] = scall2(wdcpy, -1,  0, 0, &c[3*i+2].arg[2], &r[3*i]);
		c[3*i+2] = scall3(write, -1, -1, 0, fO, buf, 0 /*SetByAbove*/);
	}
}

int main(int argc, char *argv[]) {
	struct timespec t0, t1;
	if (argc < 2) {
		fprintf(stderr, "Usage:\n\t[BATCH_EMUL=1] %s BUF_LEN [NBATCH (128)]\n"
		        "copies stdin to stdout via sys_batch or not\n",
		        argv[0]);
		return 1;
	}
	int        len   = atoi(argv[1]);
	int        n     = argc > 2 ? atoi(argv[2]) : 128;
	char      *buf   = malloc(len);   // Allocate IO, Rets, Calls Buffers
	long      *rets  = (long *)malloc(3*n * sizeof *rets);
	syscall_t *calls = (syscall_t *)malloc(3*n * sizeof *calls);
	if (!buf || !rets || !calls)
		return 1;
	mkRW(calls, rets, n, 0, 1, buf, len);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (batch(rets, calls, 3*n, 0, 0) == 3*n - 1) {
//XXX Could analyze rets for short writes/errs; Maybe print stuff, etc.
//		for (int i = 0; i < 3*n; i++)
//			fprintf(stderr, "ret%d: %ld\n", i, rets[i]);
        }
	clock_gettime(CLOCK_MONOTONIC, &t1);
	fprintf(stderr, "%ld ns\n",
	        (t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec);
	return 0;
}
