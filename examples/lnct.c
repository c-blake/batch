#include <stdio.h>
#include <stdlib.h>
#include "ftw.c"
#define UNUSE __attribute((unused))

int visited_links = 0;

void visit(const char *path UNUSE, const char *base UNUSE, statx_t *stx UNUSE) {
	visited_links++;
#ifdef PRINT
	printf("%10lld %s\t%s\n", stx->stx_size, base, path);
#endif
}

int main(int ac, char** av) {
	int i, n = 1;
	if (ac > 2)
		n = atoi(av[2]);
	for (i = 0; i < n; i++)
		ftw(ac > 1 ? av[1] : ".", visit, 1024, 1);
	fprintf(stderr, "%d links visited\n", visited_links);
	return 0;
}
