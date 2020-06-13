#include <stdio.h>
#include <stdlib.h>
#include "ftw.c"

long sum0 = 0, sum1 = 0;    // like "du -sbl", but also total stx_blocks

void visit(const char *path __attribute((unused)),
           const char *base __attribute((unused)), statx_t *stx)
{
	if (stx->stx_nlink == 0) return;
	sum0 += stx->stx_size;
	sum1 += stx->stx_blocks;
}

int main(int ac, char** av) {
	int i, n = 1;
	if (ac > 2)
		n = atoi(av[2]);
	for (i = 0; i < n; i++)
		ftw(ac > 1 ? av[1] : ".", visit, 1024, 1);
	printf("total stx_size: %ld total stx_blocks*512: %ld\n",
               sum0, sum1 * 512);
	return 0;
}
