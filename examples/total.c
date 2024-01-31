/* Open, fstat, mmap, close, and sum all files specified in av[] */
#include <fcntl.h>          /* O_ macros */
#include <stdio.h>          /* fprintf, stderr */
#include <string.h>         /* strerror() */
#include <sys/mman.h>       /* PROT_* MAP_* */
#include <sys/stat.h>       /* struct stat */
#include "linux/batch.h"    /* syscall_t, batch*|BATCH* macros */

char *mapall(char *path, long *len, int *eno) { /* could take prot/flags/etc. */
	struct stat st;
	/* Can init ret[] to -4095 (greatest abs errno) which is impossible */
	long        ret[8], r, fdAddr=(long)&ret[0], szAddr=(long)&st.st_size;
	syscall_t   b[8] = {
	    scall3(open , -1,0,0, path, O_RDONLY, 0),
	    scall2(wdcpy, -1,0,0, &b[3].arg[0], fdAddr), /* fd for fstat */
	    scall2(wdcpy, -1,0,0, &b[7].arg[0], fdAddr), /* fd for close */
	    scall2(fstat, +3,0,0, 0, &st),      /* fail => skip cpy,cpy,mmap */
	    scall2(wdcpy, -1,0,0, &b[6].arg[1], szAddr),
	    scall2(wdcpy, -1,0,0, &b[6].arg[4], fdAddr),
	    scall6(mmap , -1,0,0, 0, 0, PROT_READ, MAP_SHARED, 0, 0),
	    scall1(close, -1,0,0, 0) };
	if ((r = batch(ret, b, 8, 0, 0)) < 7) { /* rval is highest done INDEX */
		close(ret[0]);  /* close since batch didn't: fstat|mmap fail */
		if (eno) *eno = -ret[r];
		return (char *)-1;
	}
	if (len)
		*len = st.st_size;
	return (char *)ret[6];
}

int sum(char *buf, long len) {
	long i, s = 0;
	for (i = 0; i < len; i++)
		s += buf[i];
	return s;
}

int main(int ac __attribute((unused)), char **av) {
	int  eno;
	long total = 0, len = 0;
	char *p;
	for (av++; *av; av++)
	if ((p = mapall(*av, &len, &eno)) != (char *)-1)
		total += sum(p, len);
	else
		fprintf(stderr, "sum: \"%s\": %s\n", *av, strerror(eno));
	printf("grand total: %ld\n", total);
	return 0;
}
