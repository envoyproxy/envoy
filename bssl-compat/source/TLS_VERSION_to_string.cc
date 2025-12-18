#include <openssl/ssl.h>


const char *TLS_VERSION_to_string(int version) {
  switch (version) {
    case TLS1_VERSION:    return "TLSv1";
    case TLS1_1_VERSION:  return "TLSv1.1";
    case TLS1_2_VERSION:  return "TLSv1.2";
    case TLS1_3_VERSION:  return "TLSv1.3";
    case DTLS1_VERSION:   return "DTLSv1";
    case DTLS1_2_VERSION: return "DTLSv1.2";
    default:  return "???";
  }
}
