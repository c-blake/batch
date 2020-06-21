#include <stdio.h>		// printf
#include <errno.h>		// errno
#include <string.h>		// strerror
#include "linux/batch.h"	// syscall_t batch

int main(int ac, char **av) {
	syscall_t bat = { 0 };
	long      rvs = 0;
	long      ret = batch(&rvs, &bat, 0, 2, 3);
	printf("%ld: %s\n", ret, strerror(errno));
	return 0;
}
