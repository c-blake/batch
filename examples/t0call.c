#include <stdio.h>		// printf
#include "linux/batch.h"	// syscall_t batch

int main(int ac, char **av) {
	syscall_t bat = { 0 };
	long	  rvs = 0;
	printf("%ld\n", batch(&rvs, &bat, 0, 2, 3));
	return 0;
}
