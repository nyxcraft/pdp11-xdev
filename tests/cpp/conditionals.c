#define MAX 10
#if MAX > 5
int big = 1;
#else
int big = 0;
#endif
#ifdef MAX
int have_max = 1;
#endif
#ifndef FOO
int no_foo = 1;
#endif
