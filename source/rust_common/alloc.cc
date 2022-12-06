// Copied from
// https://cs.github.com/chromium/chromium/blob/42bd3d0802e90ca093450a2683ff2a104e10e48a/build/rust/std/remap_alloc.cc
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// NOLINT(namespace-envoy)

extern "C" {

void* __rdl_alloc(size_t, size_t);
void __rdl_dealloc(void*);
void* __rdl_realloc(void*, size_t, size_t, size_t);
void* __rdl_alloc_zeroed(size_t, size_t);

void* __attribute__((weak)) __rust_alloc(size_t a, size_t b) { return __rdl_alloc(a, b); }

void __attribute__((weak)) __rust_dealloc(void* a) { __rdl_dealloc(a); }

void* __attribute__((weak)) __rust_realloc(void* a, size_t b, size_t c, size_t d) {
  return __rdl_realloc(a, b, c, d);
}

void* __attribute__((weak)) __rust_alloc_zeroed(size_t a, size_t b) {
  return __rdl_alloc_zeroed(a, b);
}

void __attribute__((weak)) __rust_alloc_error_handler(size_t, size_t) { abort(); }

uint8_t __attribute__((weak)) __rust_alloc_error_handler_should_panic = 0;
}
