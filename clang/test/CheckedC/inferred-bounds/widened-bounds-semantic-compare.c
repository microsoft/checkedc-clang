// Tests for bounds widening of _Nt_array_ptr's using function to semantically
// compare two expressions.
//
// RUN: %clang_cc1 -fdump-widened-bounds -verify -verify-ignore-unexpected=note -verify-ignore-unexpected=warning %s | FileCheck %s

void f1(int i) {
  _Nt_array_ptr<char> p : bounds(p, p + i) = "a"; // expected-error {{it is not possible to prove that the inferred bounds of 'p' imply the declared bounds of 'p' after initialization}}

  if (*(i + p)) {}

// CHECK: In function: f1
// CHECK:   2: *(i + p)
// CHECK: upper_bound(p) = 1
}

void f2(int i, int j) {
  _Nt_array_ptr<char> p : bounds(p, p + (i + j)) = "a"; // expected-error {{it is not possible to prove that the inferred bounds of 'p' imply the declared bounds of 'p' after initialization}}

  if (*(p + (j + i))) {}

// CHECK: In function: f2
// CHECK:   2: *(p + (j + i))
// CHECK: upper_bound(p) = 1
}

void f3(int i, int j) {
  _Nt_array_ptr<char> p : bounds(p, p + (i * j)) = "a"; // expected-error {{it is not possible to prove that the inferred bounds of 'p' imply the declared bounds of 'p' after initialization}}

  if (*(p + (j * i))) {}

// CHECK: In function: f3
// CHECK:   2: *(p + (j * i))
// CHECK: upper_bound(p) = 1
}

void f4(int i, int j, int k, int m, int n) {
  _Nt_array_ptr<char> p : bounds(p, p + i + j + k + m + n) = "a";

  if (*(n + m + k + j + i + p)) {}

// CHECK: In function: f4
// CHECK:   2: *(n + m + k + j + i + p)
// CHECK: upper_bound(p) = 1
}

void f5(int i, int j, int k, int m, int n) {
  _Nt_array_ptr<char> p : bounds(p, (p + i) + (j + k) + (m + n)) = "a";

  if (*((n + m + k) + (j + i + p))) {}

// CHECK: In function: f5
// CHECK:   2: *((n + m + k) + (j + i + p))
// CHECK: upper_bound(p) = 1
}

void f6(int i, int j) {
  _Nt_array_ptr<char> p : bounds(p, p + i + j + i + j) = "a";

  if (*(j + j + p + i + i)) {}

// CHECK: In function: f6
// CHECK:   2: *(j + j + p + i + i)
// CHECK: upper_bound(p) = 1
}

void f7(int i, int j) {
  _Nt_array_ptr<char> p : bounds(p, p + i * j) = "a"; // expected-error {{it is not possible to prove that the inferred bounds of 'p' imply the declared bounds of 'p' after initialization}}

  if (*(p + i + j)) {}

// CHECK: In function: f7
// CHECK:   2: *(p + i + j)
// CHECK-NOT: upper_bound(p)
}

void f8(int i, int j) {
  _Nt_array_ptr<char> p : bounds(p, p + i + j) = "a";

  if (*(p + i + i)) {}

// CHECK: In function: f8
// CHECK:   2: *(p + i + i)
// CHECK-NOT: upper_bound(p)
}

void f9(int i, int j, int k) {
  _Nt_array_ptr<char> p : bounds(p, (p + i) + (j * k)) = "a";

  if (*((p + i) + (j * k))) {}

// CHECK: In function: f9
// CHECK:   2: *((p + i) + (j * k))
// CHECK: upper_bound(p) = 1
}

void f10(int i, int j, int k) {
  _Nt_array_ptr<char> p : bounds(p, (p + i) + (j * k)) = "a";

  if (*((p + i) + (j + k))) {}

// CHECK: In function: f10
// CHECK:   2: *((p + i) + (j + k))
// CHECK-NOT: upper_bound(p)
}
