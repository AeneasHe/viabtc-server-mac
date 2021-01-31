/*
 * Description: 
 *     History: yang@haipo.me, 2016/03/26, create
 */

# ifndef _UT_TITLE_H_
# define _UT_TITLE_H_

/* should call this function in the beginning of main */
void process_title_init(int argc, char *argv[]);

/* update process titile */
void process_title_set(const char *fmt, ...);

# endif

# if defined(__APPLE__) || defined(__FreeBSD__)
# define appname getprogname()
# elif defined(_GNU_SOURCE)
const char *appname = program_invocation_name;
# else
const char *appname = argv[0];
# endif

# if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
# define clearenv() 0
# endif