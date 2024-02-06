#include <stdio.h>		// printf
#include "linux/batch.h"	// syscall_t batch

int main(int ac, char **av) {
	syscall_t bat = { __NR_getuid };
	printf("%ld\n", batch(NULL, &bat, 1, 0, 0));
	return 0;          // NULL ret ^ should induce seg fault
}
