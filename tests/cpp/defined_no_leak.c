/* 2.9BSD cpp handles defined() on a *defined* macro without the 2.8BSD
 * flslvl leak: `#if defined(MAX)' does NOT suppress the following `#if'.
 * (In 2.8BSD this was a preserved bug-compatible quirk; 2.9BSD fixed it.) */
#define MAX 10
#if defined(MAX)
present = 1;
#endif
#if !defined(NEVER)
gone = 1;
#endif
