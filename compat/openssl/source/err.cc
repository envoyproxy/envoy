#include <openssl/err.h>
#include <ossl.h>

#include <cstring>
#include <map>
#include <string>

uint64_t o2b(uint64_t e) {
  switch (e) {
  case 0x0A0000C1:
    return 0x100000B8;
  case 0x0A0C0100:
    return 0x10000041;
  case 0x0A000076:
    return 0x100000FD;
  case 0x0A000102:
    return 0x100000F0;
  case 0x0A000086:
    return 0x1000007D;
  }
  return e;
}

uint64_t b2o(uint64_t e) {
  switch (e) {
  case 0x100000B8:
    return 0x0A0000C1;
  case 0x10000041:
    return 0x0A0C0100;
  case 0x100000FD:
    return 0x0A000076;
  case 0x100000F0:
    return 0x0A000102;
  case 0x1000007D:
    return 0x0A000086;
  }
  return e;
}

extern "C" char* ERR_error_string(uint32_t packed_error, char* buf) {
  return ossl.ossl_ERR_error_string(b2o(packed_error), buf);
}

extern "C" char* ERR_error_string_n(uint32_t packed_error, char* buf, size_t len) {
  /**
   * For test code that checks for error *text* we put an entry in this map so
   * that we get exactly the same text that BoringSSL would produce for the same
   * error. This is needed because OpenSSL and BoringSSL packed error codes aren't
   * numerically the same, and BoringSSL always puts "OPENSSL_internal" as the
   * function name.
   */
  static const std::map<uint32_t, const char*> ERRORMAP{
      {0x10000041, "error:10000041:SSL routines:OPENSSL_internal:malloc failure"},
      {0x100000fd, "error:100000fd:SSL routines:OPENSSL_internal:NO_COMMON_SIGNATURE_ALGORITHMS"},
      {0x1e08010c, "error:0900006e:PEM routines:OPENSSL_internal:NO_START_LINE"},
      {0x05800074,
       "error:0b000074:X.509 certificate routines:OPENSSL_internal:KEY_VALUES_MISMATCH"},
      {0x100000F0,
       "TLS_error:|268435696:SSL routines:OPENSSL_internal:UNSUPPORTED_PROTOCOL:TLS_error_end"},
      {0x1000007D,
       "TLS_error:|268435581:SSL routines:OPENSSL_internal:UNSUPPORTED_PROTOCOL:TLS_error_end"},
  };

  auto i = ERRORMAP.find(packed_error);
  if (i != ERRORMAP.end()) {
    strncpy(buf, i->second, len);
    buf[len - 1] = '\0';
    return buf;
  }

  return ossl.ossl_ERR_error_string_n(b2o(packed_error), buf, len), buf;
}

extern "C" const char* ERR_func_error_string(uint32_t packed_error) { return "OPENSSL_internal"; }

extern "C" uint32_t ERR_get_error(void) { return o2b(ossl.ossl_ERR_get_error()); }

extern "C" const char* ERR_lib_error_string(uint32_t packed_error) {
  // Handle system library errors like BoringSSL does
  // OpenSSL 3.x returns NULL for system errors, but BoringSSL returns "system library"
  if (ossl_ERR_SYSTEM_ERROR(packed_error)) {
    return "system library";
  }

  const char* ret = ossl.ossl_ERR_lib_error_string(b2o(packed_error));
  return (ret ? ret : "unknown library");
}

extern "C" uint32_t ERR_peek_error(void) { return o2b(ossl.ossl_ERR_peek_error()); }

extern "C" uint32_t ERR_peek_error_line_data(const char** file, int* line, const char** data,
                                             int* flags) {
  return o2b(ossl.ossl_ERR_peek_error_line_data(file, line, data, flags));
}

extern "C" uint32_t ERR_peek_last_error(void) { return o2b(ossl.ossl_ERR_peek_last_error()); }

extern "C" const char* ERR_reason_error_string(uint32_t packed_error) {
  // Handle system library errors like BoringSSL does
  // For system errors, return strerror(errno) like BoringSSL does
  if (ossl_ERR_SYSTEM_ERROR(packed_error)) {
    uint32_t reason = ossl_ERR_GET_REASON(packed_error);
    // For some reason BoringSSL only calls strerror() for errno < 127, and just
    // returns the string "unknown error" for errno >= 127. This excludes some
    // valid errno values (on Linux at least). We delibrately differ from
    // BoringSSL here, and up the limit to 256 to ensure we don't hide any valid
    // error strings.
    if (reason > 0 && reason < 256) {
      return strerror(reason);
    }
    return "unknown error";
  }

  /**
   * This is not an exhaustive list of errors; rather it is just the ones that
   * need to be translated for the Envoy tests to pass (yes some of the tests do
   * check for specific error *text*).
   */
  static const std::map<std::string, std::string> ossl_2_bssl_error_string_map{
      {"sslv3 alert certificate expired", "SSLV3_ALERT_CERTIFICATE_EXPIRED"},
      {"sslv3 alert handshake failure", "SSLV3_ALERT_HANDSHAKE_FAILURE"},
      {"tlsv1 alert protocol version", "TLSV1_ALERT_PROTOCOL_VERSION"},
      {"tlsv1 alert unknown ca", "TLSV1_ALERT_UNKNOWN_CA"},
      {"unsupported protocol", "UNSUPPORTED_PROTOCOL"},
      {"no shared cipher", "NO_SHARED_CIPHER"},
      {"no suitable signature algorithm", "NO_COMMON_SIGNATURE_ALGORITHMS"},
      {"certificate verify failed", "CERTIFICATE_VERIFY_FAILED"},
  };

  const char* result = ossl.ossl_ERR_reason_error_string(b2o(packed_error));

  if (result == nullptr) {
    result = "unknown error";
  } else {
    auto i = ossl_2_bssl_error_string_map.find(result);
    if (i != ossl_2_bssl_error_string_map.end()) {
      result = i->second.c_str();
    }
  }

  return result;
}

/*
 * This function doesn't get automatically generated into ossl.c by
 * the prefixer because it doesn't understand how to deal with the varargs.
 */
void ossl_ERR_set_error(int lib, int reason, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ossl.ossl_ERR_vset_error(lib, reason, fmt, args);
  va_end(args);
}
