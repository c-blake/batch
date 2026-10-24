/* Linux batch syscall tester: execvp(3) via one batch system call.  Entry i is
   execve(DIRi/CMD).  Failure goes to the next slot, as does a shell's $PATH
   search.  Success never returns here; sys_batch ends the batch for exec (the
   0 jumps are -1 in case that is ever not true).  batch() returns only if every
   execve failed, leaving every errno in ret[] to report like execvp(3) does:
   EACCES from any dir wins.  Like exec(3), "no $PATH search if CMD has a '/'".
   This does not do the /bin/sh fallback for ENOEXEC. */
#include <stdio.h>          /* fprintf, perror */
#include <stdlib.h>         /* calloc, malloc, getenv */
#include <string.h>         /* strchr, strdup, strsep, strlen, strerror */
#include "linux/batch.h"    /* batching interface */

extern char **environ;

int main(int ac, char **av) {
	char      *cmd, *pa, *s, *p, *d, *path;
	long      *ret, r;
	syscall_t *b;
	int        n, i, eno;
	if (ac < 2)
		return fprintf(stderr, "Usage: %s CMD [ARG..]\n", av[0]), 1;
	cmd = av[1];
	pa  = strchr(cmd, '/') ? "" : getenv("PATH"); // ""=>1 dir ""=>path=cmd
	if (!pa)
		pa = "/bin:/usr/bin";
	for (n = 1, p = pa; *p; p++)    // Dirs = ':' + 1; "" is cwd, as in sh
		n += *p == ':';
	if (!(b = calloc(n, sizeof *b)) || !(ret = calloc(n, sizeof *ret)) ||
	    !(s = strdup(pa)))
		return perror("calloc"), 1;
	for (i = 0; i < n; i++) {       // Leaks all path[i]; exec/exit cleans up
		d = strsep(&s, ":");
		if (!(path = malloc(strlen(d) + strlen(cmd) + 2)))
			return perror("malloc"), 1;
		sprintf(path, "%s%s%s", d, *d ? "/" : "", cmd);
		b[i] = scall3(execve, 0, -1, -1, path, av + 1, environ);
	}
	for (i = 0; i < n; i += r + 1) {        // Only returns if no execve OK
		if ((r = batch(ret + i, b + i, n - i, 0, 0)) < 0)
			return perror("batch"), 1;
		if (r == n - i - 1)             // All failed; Else signal halted
			break;                  //..early, so resume after slot r
	}
	for (eno = -ret[n - 1], i = 0; i < n; i++)
		if (ret[i] == -EACCES)
			eno = EACCES;
	fprintf(stderr, "%s: %s: %s\n", av[0], cmd, strerror(eno));
	return eno == EACCES ? 126 : 127;
}
