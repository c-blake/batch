/* Test that work that should be run before return to user space still happens
   with sys_batch.  close(2) alone is synchronous since Linux ~6.8, but dup2
   over an fd still defers.  This tries both. */
#include <stdio.h>          /* printf perror */
#include <string.h>         /* memset */
#include <fcntl.h>          /* open */
#include <signal.h>         /* sigaction alarm */
#include <unistd.h>         /* pipe close */
#include "linux/batch.h"    /* batching interface */

static void onAlrm(int s) { (void)s; }

static int try(const char *name, int how, int nul) {
	int       p[2];
	char      c;
	long      ret[2] = { 99, 99 }, r;
	syscall_t b[2];
	if (pipe(p))
		return perror("pipe"), 1;
	b[0] = how ? scall2(dup2 , 0,0,0, nul, p[1])   /* clobber only writer */
	           : scall1(close, 0,0,0, p[1]);       /* ..or close it */
	b[1] = scall3(read, 0,0,0, p[0], &c, 1);       /* EOF iff released */
	alarm(2);
	r = batch(ret, b, 2, 0, 0);
	alarm(0);
	printf("%-5s batch=%ld sub0=%ld read=%ld => %s\n", name, r, ret[0], ret[1],
	       ret[1] == 0 ? "released in time (sequential semantics)" :
	       "release DEFERRED past next subcall");
	close(p[0]);
	if (how)
		close(p[1]);           /* dup2 left p[1] open (as /dev/null) */
	return ret[1] != 0;
}

int main(void) {
	struct sigaction sa;
	int nul = open("/dev/null", O_RDWR), bad;
	memset(&sa, 0, sizeof sa);     /* No SA_RESTART: let it interrupt */
	sa.sa_handler = onAlrm;
	sigaction(SIGALRM, &sa, 0);
	bad  = try("close", 0, nul);
	bad |= try("dup2" , 1, nul);
	return bad;
}
