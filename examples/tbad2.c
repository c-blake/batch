#include <stdio.h>		// printf
#include "linux/batch.h"	// syscall_t batch

int main(int ac, char **av) {
	syscall_t bat __attribute((unused)) = { __NR_getuid };
	long	  rv  = 0; // pass |||| bad address
	printf("%ld\n", batch(&rv, NULL, 1, 0, 0));
	printf("%ld %d\n", rv, errno);  // errno -l|grep this should == EFAULT
	return 0;
}
