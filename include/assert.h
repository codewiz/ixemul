/*
 * assert.h,v
 *
 * Revision 1.1  2026/10/26  ChatGPT modifications (JJ)
 *
 *    Use ixemul stdio and abort() instead of libgcc __eprintf().
 *    Preserve legacy compiler branches and NDEBUG handling.
 */
/* Allow this file to be included multiple times
   with different settings of NDEBUG.  */
#undef assert
#undef __assert

#ifdef NDEBUG
#define assert(ignore) ((void) 0)
#else

#include <stdio.h>
#include <stdlib.h>

#ifndef __GNUC__

#define assert(expression)  \
  ((void) ((expression) ? 0 : __assert (expression, __FILE__, __LINE__)))

#define __assert(expression, file, lineno)  \
  (fprintf (stderr, "%s:%d: failed assertion\n", file, lineno),	\
   abort (), 0)

#else

#if defined(__STDC__) || defined (__cplusplus)

#define assert(expression)  \
  ((void) ((expression) ? 0 : __assert (#expression, __FILE__, __LINE__)))

#define __assert(expression, file, line)  \
  (fprintf (stderr, "%s:%d: failed assertion `%s'\n",		\
	      file, line, expression), abort (), 0)

#else /* no __STDC__ and not C++; i.e. -traditional.  */

#define assert(expression)  \
  ((void) ((expression) ? 0 : __assert (expression, __FILE__, __LINE__)))

#define __assert(expression, file, lineno)  \
  (fprintf (stderr, "%s:%d: failed assertion `%s'\n",		\
	      file, lineno, "expression"), abort (), 0)

#endif /* no __STDC__ and not C++; i.e. -traditional.  */
#endif /* no __GNU__; i.e., /bin/cc.  */
#endif
