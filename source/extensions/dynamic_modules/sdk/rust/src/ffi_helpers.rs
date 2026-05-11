//! Safe wrappers over `slice::from_raw_parts` and `str::from_utf8_unchecked` for the FFI seam.
//!
//! Two well-known footguns motivate this module:
//!
//! 1. `slice::from_raw_parts(ptr, len)` is undefined behaviour if `ptr` is null, even when `len ==
//!    0`. Per the [Rust reference][1], the pointer must be non-null and aligned regardless of
//!    length. C ABIs routinely pass `(nullptr, 0)` to mean "empty".
//!
//! 2. `str::from_utf8_unchecked` is undefined behaviour on invalid UTF-8. Many ABI buffers carry
//!    attacker-influenced bytes (HTTP header values, TLS SNI, EDS metadata) that are not
//!    contractually UTF-8.
//!
//! [1]: https://doc.rust-lang.org/std/slice/fn.from_raw_parts.html#safety
//!
//! These helpers are `#[doc(hidden)] pub` so SDK-provided macros that expand in user crates
//! can reach them; users should call them via the macros rather than directly.

use std::borrow::Cow;

/// Constructs a `&[T]` slice from an FFI-supplied `(ptr, len)` pair.
///
/// Common element types include `u8` (byte buffers), `EnvoyBuffer` (chunk arrays), and
/// `(EnvoyBuffer, EnvoyBuffer)` (header-pair arrays).
///
/// - When `ptr` is null, returns an empty slice regardless of `len`. The C convention of passing
///   `(nullptr, 0)` for "empty" is the well-defined path. A non-zero `len` paired with a null `ptr`
///   is a caller-side contract violation; this function logs an error via `envoy_log_error!` and
///   still returns an empty slice rather than dereferencing.
/// - Otherwise dereferences `(ptr, len)` exactly as `std::slice::from_raw_parts` would.
///
/// # Safety
///
/// When `ptr` is non-null, the caller must guarantee:
///   - `ptr` is properly aligned for `T`.
///   - `ptr` points to `len` consecutive initialised values of `T`.
///   - The memory outlives the lifetime `'a`. `'a` is freely chosen by the caller; choosing a
///     lifetime longer than the underlying memory is a contract violation.
///
/// When `ptr` is null this function never reads any memory; passing `(nullptr, 0)` is safe.
#[inline]
#[doc(hidden)]
pub unsafe fn slice_from_raw_or_empty<'a, T>(ptr: *const T, len: usize) -> &'a [T] {
  if ptr.is_null() {
    if len != 0 {
      crate::envoy_log_error!(
        "ffi_helpers::slice_from_raw_or_empty: null ptr with len={len} (treating as empty)"
      );
    }
    return &[];
  }
  // Safety: caller upholds the non-null branch's invariants.
  unsafe { std::slice::from_raw_parts(ptr, len) }
}

/// Mutable variant of [`slice_from_raw_or_empty`]. Same semantics; same null-ptr handling.
///
/// # Safety
///
/// Same constraints as [`slice_from_raw_or_empty`], plus exclusive-borrow on the underlying
/// values for the lifetime `'a`.
#[inline]
#[doc(hidden)]
pub unsafe fn slice_from_raw_or_empty_mut<'a, T>(ptr: *mut T, len: usize) -> &'a mut [T] {
  if ptr.is_null() {
    if len != 0 {
      crate::envoy_log_error!(
        "ffi_helpers::slice_from_raw_or_empty_mut: null ptr with len={len} (treating as empty)"
      );
    }
    // `&mut []` is a valid empty mutable slice of any lifetime; no memory is ever written.
    return &mut [];
  }
  // Safety: caller upholds the non-null branch's invariants.
  unsafe { std::slice::from_raw_parts_mut(ptr, len) }
}

/// Lossy UTF-8 conversion of an FFI-supplied byte buffer.
///
/// Always succeeds: invalid UTF-8 is replaced with `U+FFFD REPLACEMENT CHARACTER` per
/// [`String::from_utf8_lossy`]. Returns `Cow<'a, str>` so the common case of valid UTF-8
/// avoids allocation.
///
/// Use this helper for any ABI buffer where UTF-8 is the desired representation but not
/// contractually guaranteed. Examples: filter / cluster / module names from operator config,
/// HTTP header values, SNI strings, EDS metadata keys/values.
///
/// Callers that need "invalid UTF-8 maps to empty" semantics (for example, the
/// `declare_matcher!` macro, which uses the name as a registry key where `U+FFFD` substitution
/// would route to a different entry) should call [`slice_from_raw_or_empty`] followed by
/// `str::from_utf8(...).unwrap_or("")` instead.
///
/// # Safety
///
/// Same constraints as [`slice_from_raw_or_empty`]. Passing `(nullptr, 0)` is safe and
/// produces an empty `Cow::Borrowed("")`.
#[inline]
#[doc(hidden)]
pub unsafe fn str_lossy_from_raw<'a>(ptr: *const u8, len: usize) -> Cow<'a, str> {
  let bytes = unsafe { slice_from_raw_or_empty::<'a>(ptr, len) };
  String::from_utf8_lossy(bytes)
}

#[cfg(test)]
mod tests {
  use super::*;

  #[test]
  fn slice_from_null_ptr_with_zero_len_is_empty() {
    // Safety: null ptr with zero len is the documented safe path.
    let slice: &[u8] = unsafe { slice_from_raw_or_empty(std::ptr::null(), 0) };
    assert!(slice.is_empty());
  }

  #[test]
  fn slice_from_valid_ptr_round_trips() {
    let data = b"hello";
    // Safety: the borrow lives through the call.
    let slice: &[u8] = unsafe { slice_from_raw_or_empty(data.as_ptr(), data.len()) };
    assert_eq!(slice, b"hello");
  }

  #[test]
  fn str_lossy_handles_valid_utf8_without_alloc() {
    let data = b"hello";
    // Safety: the borrow lives through the call.
    let s = unsafe { str_lossy_from_raw(data.as_ptr(), data.len()) };
    assert_eq!(s, "hello");
    assert!(matches!(s, Cow::Borrowed(_)));
  }

  #[test]
  fn str_lossy_replaces_invalid_utf8() {
    // 0xff is never a valid UTF-8 byte.
    let data = [0xff_u8, b'a'];
    // Safety: the borrow lives through the call.
    let s = unsafe { str_lossy_from_raw(data.as_ptr(), data.len()) };
    // U+FFFD followed by 'a'.
    assert_eq!(s.as_ref(), "\u{fffd}a");
    assert!(matches!(s, Cow::Owned(_)));
  }

  #[test]
  fn str_lossy_from_null_ptr_is_empty() {
    // Safety: null ptr with zero len is the documented safe path.
    let s = unsafe { str_lossy_from_raw(std::ptr::null(), 0) };
    assert_eq!(s, "");
  }

  #[test]
  fn slice_mut_from_null_ptr_with_zero_len_is_empty() {
    // Safety: null ptr with zero len is the documented safe path.
    let slice: &mut [u8] = unsafe { slice_from_raw_or_empty_mut(std::ptr::null_mut(), 0) };
    assert!(slice.is_empty());
  }

  #[test]
  fn slice_mut_from_valid_ptr_round_trips() {
    let mut data: [u8; 5] = *b"hello";
    // Safety: the borrow lives through the call.
    let slice: &mut [u8] = unsafe { slice_from_raw_or_empty_mut(data.as_mut_ptr(), data.len()) };
    slice[0] = b'H';
    assert_eq!(&data, b"Hello");
  }

  #[test]
  fn slice_from_null_ptr_with_nonzero_len_returns_empty_and_does_not_panic() {
    // Contract violation path: caller passed `(null, len > 0)`. The helper logs an error
    // and returns an empty slice rather than dereferencing.
    // Safety: null ptr is the documented safe-but-misuse path.
    let slice: &[u8] = unsafe { slice_from_raw_or_empty(std::ptr::null(), 5) };
    assert!(slice.is_empty());
  }

  #[test]
  fn slice_mut_from_null_ptr_with_nonzero_len_returns_empty_and_does_not_panic() {
    // Safety: null ptr is the documented safe-but-misuse path.
    let slice: &mut [u8] = unsafe { slice_from_raw_or_empty_mut(std::ptr::null_mut(), 5) };
    assert!(slice.is_empty());
  }

  #[test]
  fn slice_from_struct_ptr_round_trips() {
    // Generic over arbitrary `T`; verify with an `(EnvoyBuffer, EnvoyBuffer)`-shaped tuple.
    #[repr(C)]
    #[derive(Copy, Clone, Debug, PartialEq)]
    struct Pair {
      a: u32,
      b: u32,
    }
    let data = [
      Pair { a: 1, b: 2 },
      Pair { a: 3, b: 4 },
      Pair { a: 5, b: 6 },
    ];
    // Safety: the borrow lives through the call.
    let slice: &[Pair] = unsafe { slice_from_raw_or_empty(data.as_ptr(), data.len()) };
    assert_eq!(slice, &data);
  }

  #[test]
  fn slice_from_null_struct_ptr_with_nonzero_len_returns_empty() {
    #[repr(C)]
    struct Pair {
      _a: u32,
      _b: u32,
    }
    // Safety: null ptr is the documented safe-but-misuse path.
    let slice: &[Pair] = unsafe { slice_from_raw_or_empty(std::ptr::null::<Pair>(), 5) };
    assert!(slice.is_empty());
  }
}
