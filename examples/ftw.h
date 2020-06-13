#ifndef FTW_H
#define FTW_H
#ifdef __cplusplus
extern "C" {
#endif
#include "mstatx.h"
/*
  ftw -- file tree walk: recursively apply visit() to every link below path with
  dirs visted post kids.  Un-stat-able files get stx.stx_nlink==0, impossible
  normally, while listable but unreadable directories get stx.stx_dev_major==0.
*/
typedef void (*ftw_f)(const char *path, const char *base, statx_t *stx);

void ftw(const char *path, ftw_f visit, int maxDepth, int xdev);

#ifdef __cplusplus
}
#endif
#endif /* FTW_H */
