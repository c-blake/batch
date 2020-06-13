#include <stdio.h>		// printf
#include "linux/batch.h"	// syscall_t batch

int main(int ac, char **av) {
	syscall_t bat = { 0 };
	long	  rv = 0;
	bat.nr = -1;
	printf("%ld\n", batch(&rv, &bat, 1, 2, 3));
	printf("%ld\n", rv);
	return 0;
}
