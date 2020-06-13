#include <stdio.h>		// printf
#include "linux/batch.h"	// syscall_t batch

int main(int ac, char **av) {
	syscall_t bat = { __NR_getuid };
	long	  rv  = 0;
	printf("%ld\n", batch(&rv, &bat, 1, 0, 0));
	printf("%ld\n", rv);
	return 0;
}
