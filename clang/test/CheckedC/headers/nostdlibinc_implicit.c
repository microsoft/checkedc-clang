// The -nostdlibinc option suppresses the search for include files in standard
// system include directories like /usr/local/include, /usr/include,
// /usr/include/<TARGET_ARCH>. But the <BUILD>/lib/clang/<VERSION>/include
// directory is still searched for include files.
// Therefore, in the context of a Checked C compilation, all the wrapper
// header files listed below are found while the corresponding system header
// file that is included in each of these files (via #include_next) is not
// found when -nostdlibinc is specified on the compilation command line.
//
// This test confirms the above behavior with the following checks:
//   1) The wrapper header files listed below are found.
//   2) The system header file of the same name that each wrapper header file
//      includes (via #include_next), is not found.
// The -MM -MG preprocessor options list the header files not found.
//
// RUN: %clang -target x86_64-unknown-unknown -nostdlibinc -ffreestanding -MM -MG %s | FileCheck %s


#if defined(__has_include)

#if __has_include (<assert.h>)
#include <assert.h>
#endif
// CHECK: assert.h
#if __has_include (<errno.h>)
#include <errno.h>
#endif
// CHECK: errno.h
#if __has_include (<fcntl.h>)
#include <fcntl.h>
#endif
// CHECK: fcntl.h
#if __has_include (<fenv.h>)
#include <fenv.h>
#endif
// CHECK: fenv.h
#if __has_include (<inttypes.h>)
#include <inttypes.h>
#endif
// CHECK: inttypes.h
#if __has_include (<math.h>)
#include <math.h>
#endif
// CHECK: math.h
#if __has_include (<signal.h>)
#include <signal.h>
#endif
// CHECK: signal.h
#if __has_include (<stdio.h>)
#include <stdio.h>
#endif
// CHECK: stdio.h
#if __has_include (<stdlib.h>)
#include <stdlib.h>
#endif
// CHECK: stdlib.h
#if __has_include (<string.h>)
#include <string.h>
#endif
// CHECK: string.h
#if __has_include (<sys/stat.h>)
#include <sys/stat.h>
#endif
// CHECK: stat.h
#if __has_include (<time.h>)
#include <time.h>
#endif
// CHECK: time.h

// Wrapper header files for system header files that don't exist on Windows
// cannot be added here because they test for the system header via
// __has_include_next and generate an `#error` instead of a -MG entry for the
// system header.

#endif
