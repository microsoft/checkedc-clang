// The -nostdinc option suppresses the search for include files in standard
// system include directories like /usr/local/include, /usr/include,
// /usr/include/<TARGET_ARCH>, and also the <BUILD>/lib/clang/<VERSION>/include
// directory.
// Therefore, in the context of a Checked C compilation, none of the
// header files listed below will be found when -nostdinc is specified on the
// compilation command line.
//
// This test confirms the above behavior.
//
// RUN: %clang -target x86_64-unknown-unknown \
// RUN:   -nostdinc -ffreestanding -fsyntax-only %s

#if defined(__has_include)

#if __has_include (<assert.h>) \
 || __has_include (<errno.h>) \
 || __has_include (<fcntl.h>) \
 || __has_include (<fenv.h>) \
 || __has_include (<inttypes.h>) \
 || __has_include (<math.h>) \
 || __has_include (<signal.h>) \
 || __has_include (<stdio.h>) \
 || __has_include (<stdlib.h>) \
 || __has_include (<string.h>) \
 || __has_include (<sys/stat.h>) \
 || __has_include (<threads.h>) \
 || __has_include (<time.h>) \
 || __has_include (<checkedc_extensions.h>) \
 || __has_include (<arpa/inet.h>) \
 || __has_include (<grp.h>) \
 || __has_include (<netdb.h>) \
 || __has_include (<poll.h>) \
 || __has_include (<pwd.h>) \
 || __has_include (<sys/socket.h>) \
 || __has_include (<syslog.h>) \
 || __has_include (<unistd.h>) \
 || __has_include (<utime.h>)
#error "expected to *not* be able to find standard C headers"
#endif


#if __has_include (<assert_checked.h>) \
 || __has_include (<errno_checked.h>) \
 || __has_include (<fcntl_checked.h>) \
 || __has_include (<fenv_checked.h>) \
 || __has_include (<inttypes_checked.h>) \
 || __has_include (<math_checked.h>) \
 || __has_include (<signal_checked.h>) \
 || __has_include (<stdio_checked.h>) \
 || __has_include (<stdlib_checked.h>) \
 || __has_include (<string_checked.h>) \
 || __has_include (<sys/stat_checked.h>) \
 || __has_include (<threads_checked.h>) \
 || __has_include (<time_checked.h>) \
 || __has_include (<checkedc_extensions.h>) \
 || __has_include (<arpa/inet_checked.h>) \
 || __has_include (<grp_checked.h>) \
 || __has_include (<netdb_checked.h>) \
 || __has_include (<poll_checked.h>) \
 || __has_include (<pwd_checked.h>) \
 || __has_include (<sys/socket_checked.h>) \
 || __has_include (<syslog_checked.h>) \
 || __has_include (<unistd_checked.h>) \
 || __has_include (<utime_checked.h>)
#error "expected to *not* be able to find Checked C headers"
#endif

#endif
