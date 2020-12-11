// NOLINT(namespace-envoy)
#pragma once

#include "unicode/utypes.h"

// Initialize UIDNAInfo object with UIDNA_ERROR_DISALLOWED as its errors (any non-zero value should
// work).
#define UIDNA_INFO_INITIALIZER                                                                     \
  { 0x80 }

// This enum is declared to satisfy uidna_openUTS46 when initializing UIDNAWrapper
// https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#50.
enum { UIDNA_CHECK_BIDI = 4 };

// The fake UIDNA.
struct UIDNA {};

// The fake UIDNAInfo.
struct UIDNAInfo {
  uint32_t errors;
};

// This is called by IDNToASCII
// (https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#85)
// which is responsible for converting the Unicode input representing a hostname to ASCII using IDN
// rules. In this shimmed implementation, we always set the error code to U_ILLEGAL_ARGUMENT_ERROR
// and info errors to UIDNA_ERROR_DISALLOWED, hence the coversion always fails.
int32_t uidna_nameToASCII(const UIDNA*, const UChar*, int32_t, UChar*, int32_t, UIDNAInfo* info,
                          UErrorCode* error_code) {
  *error_code = U_ILLEGAL_ARGUMENT_ERROR;
  return 0;
}

// This is called inside the UIDNAWrapper
// (https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#50)
// and should be accessed only through GetUIDNA
// (https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#66).
// This pattern is used inside GURL to initialize a static value at first use. It returns an
// instance of UIDNA to satisfy the check (GURL_DCHECK(uidna != nullptr);) here:
// https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#89.
UIDNA* uidna_openUTS46(uint32_t options, UErrorCode* error_code) { return new UIDNA; }

// While this is never be called, it needs to be defined since:
// https://quiche.googlesource.com/googleurl/+/ef0d23689e240e6c8de4c3a5296b209128c87373/url/url_idna_icu.cc#53.
const char* u_errorName(UErrorCode) { return "U_ILLEGAL_ARGUMENT_ERROR"; }
