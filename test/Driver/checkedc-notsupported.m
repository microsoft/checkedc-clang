// Checked C extension is not supported for Objective C.   Make sure driver
// rejects the flag.
//
// RUN: not %clang -fcheckedc-extension %s 2>&1 | FileCheck %s
// CHECK: error: invalid argument '-fcheckedc-extension' not allowed with 'Objective C'
//
// Have clang compile this file as a C file
// RUN: %clang -c -fcheckedc-extension -x c %s
//
// Have clang-cl to to compile this file as a C file
// RUN: %clang_cl -c -Xclang -fcheckedc-extension /TC %s

extern void f() {}


