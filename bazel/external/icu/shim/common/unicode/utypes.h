// NOLINT(namespace-envoy)
#pragma once

#include <cstdint>

using UBool = bool;
using UChar = uint16_t;

enum UErrorCode {
  // This value is used for initializing error code in this line:
  // https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#91.
  U_ZERO_ERROR = 0,

  // This makes info.errors in this line:
  // https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#102
  // to be non-zero.
  U_ILLEGAL_ARGUMENT_ERROR = 1,

  // Required in this line:
  // https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#102.
  U_BUFFER_OVERFLOW_ERROR = 15
};

// This is called inside IDNToASCII
// (https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#95),
// this should always return false.
static inline UBool U_SUCCESS(UErrorCode) { return false; }

// This is called by UIDNAWrapper constructor
// (https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#51).
// This should always return false, since the initialization of UIDNAWrapper should always be
// successful.
static inline UBool U_FAILURE(UErrorCode) { return false; }
