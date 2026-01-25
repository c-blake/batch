/* Open, fstat, mmap, close, and sum all files specified in av[] */
#include <fcntl.h>          /* O_ macros */
#include <stdio.h>          /* fprintf, stderr */
#include <string.h>         /* strerror() */
#include <sys/mman.h>       /* PROT_* MAP_* */
#include <sys/stat.h>       /* struct stat */
#include "linux/batch.h"    /* syscall_t, scall<N> macros, batch */

/* Below, `b` is a "manually compiled/flattened" version of the 4 calls:
  if ((fd = open(path, O_RDONLY) >= 0)) {   // -1,0,0 => continue only if >= 0
    fstat(fd, &st);                         // error ignored; Could check later
    void *rval = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);                              // error ignored; Could check later
  } // return value of batch() indicates amount of progress made. */
char *mapall(char *path, long *len, int *eno) { /* could take prot/flags/etc. */
	struct stat st;
	/* Might also init ret[] to impossible -4095 (greatest abs errno) */
	long        ret[10]={0}, fdAdr=(long)&ret[0], szAdr=(long)&st.st_size,r;
	syscall_t b[10]={/* < = >  0            1  2  3  4  5 .arg[]s */
	/*0*/scall3(open , -1,0,0, path, O_RDONLY, 0),   // get fd
	/*1*/scall2(wdcpy, -1,0,0, &b[4].arg[0], fdAdr), // ->fstat
	/*2*/scall2(wdcpy, -1,0,0, &b[8].arg[0], fdAdr), // ->close post mapFail
	/*3*/scall2(wdcpy, -1,0,0, &b[9].arg[0], fdAdr), // ->close post mapOk
	/*4*/scall2(fstat, +3,0,0, 0, &st),              // <0 =>skip cp,cp,mmap
	/*5*/scall2(wdcpy, -1,0,0, &b[7].arg[1], szAdr), // sz to mmap
	/*6*/scall2(wdcpy, -1,0,0, &b[7].arg[4], fdAdr), // fd to mmap
	/*7*/scall6(mmap ,  0,1,1, 0, 0, PROT_READ, MAP_SHARED, 0, 0),
	/*8*/scall1(close, -1,-1,-1, 0),   // -1s: Always Exit; For the < 9 test
	/*9*/scall1(close, -1,0,0, 0) };   // Test ret[9] in user space
	if ((r = batch(ret, b, 10, 0, 0)) < 9) { // rval is highest done INDEX
		if (eno) {      // Might instead return all of ret[]|A thinning
			while (r > 0 && ret[r] >= 0) r--;
			*eno = -ret[r];     // but the call instigating exit..
		}                           //..is not a bad simulation.
		return (char *)-1;
	}
	if (ret[9] < 0) {       // If all >=0 but final close, re-try the close.
		*eno = -ret[9]; // Like fstat fail, this can only occur on odd/
		close(ret[0]);  //..remote FSes. This leaks like close-fail post
	}                       //..fail-mmap, but it's all we can really do.
	if (len)
		*len = st.st_size;
	return (char *)ret[7];
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
			fprintf(stderr,"sum: \"%s\": %s\n", *av, strerror(eno));
	printf("grand total: %ld\n", total);
	return 0;
}
