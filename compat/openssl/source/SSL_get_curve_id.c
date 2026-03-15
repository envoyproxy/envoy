#include <openssl/ssl.h>
#include <ossl.h>
#include "log.h"


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L2345
 * https://www.openssl.org/docs/man3.0/man3/SSL_get_negotiated_group.html
 */
uint16_t SSL_get_curve_id(const SSL *ssl) {
  int nid = ossl.ossl_SSL_get_negotiated_group((SSL*)ssl);
/*
 * The data was obtained by:
 * wget https://www.iana.org/assignments/tls-parameters/tls-parameters-8.csv
 * cat tls-parameters-8.csv | grep -v Unassigned | grep -v Reserved | awk -F ',' '{ print "case ossl_NID_"$2": return "$1";"}' |sed -e 's/ (OBSOLETE)//'
 * Plus some additional manual adjustments,
 * as NID to GROUP ID mapping is not always based on the exactly same names. In general I was trying to
 * state here all groups from ssl/t1_trce.c, see `static const ssl_trace_tbl ssl_groups_tbl[]`
 */
  switch(nid) {
    case ossl_NID_sect163k1: return 1;
    case ossl_NID_sect163r1: return 2;
    case ossl_NID_sect163r2: return 3;
    case ossl_NID_sect193r1: return 4;
    case ossl_NID_sect193r2: return 5;
    case ossl_NID_sect233k1: return 6;
    case ossl_NID_sect233r1: return 7;
    case ossl_NID_sect239k1: return 8;
    case ossl_NID_sect283k1: return 9;
    case ossl_NID_sect283r1: return 10;
    case ossl_NID_sect409k1: return 11;
    case ossl_NID_sect409r1: return 12;
    case ossl_NID_sect571k1: return 13;
    case ossl_NID_sect571r1: return 14;
    case ossl_NID_secp160k1: return 15;
    case ossl_NID_secp160r1: return 16;
    case ossl_NID_secp160r2: return 17;
    case ossl_NID_secp192k1: return 18;
    case ossl_NID_X9_62_prime192v1: return 19;
    case ossl_NID_secp224k1: return 20;
    case ossl_NID_secp224r1: return 21;
    case ossl_NID_secp256k1: return 22;
    case ossl_NID_X9_62_prime256v1: return 23;
    case ossl_NID_secp384r1: return 24;
    case ossl_NID_secp521r1: return 25;
    case ossl_NID_brainpoolP256r1: return 26;
    case ossl_NID_brainpoolP384r1: return 27;
    case ossl_NID_brainpoolP512r1: return 28;
    case ossl_NID_X25519: return 29;
    case ossl_NID_X448: return 30;
    //case ossl_NID_brainpoolP256r1tls13: return 31;
    //case ossl_NID_brainpoolP384r1tls13: return 32;
    //case ossl_NID_brainpoolP512r1tls13: return 33;
    //case ossl_NID_GC256A: return 34;
    //case ossl_NID_GC256B: return 35;
    //case ossl_NID_GC256C: return 36;
    //case ossl_NID_GC256D: return 37;
    //case ossl_NID_GC512A: return 38;
    //case ossl_NID_GC512B: return 39;
    //case ossl_NID_GC512C: return 40;
    //case ossl_NID_curveSM2: return 41;
    case ossl_NID_ffdhe2048: return 256;
    case ossl_NID_ffdhe3072: return 257;
    case ossl_NID_ffdhe4096: return 258;
    case ossl_NID_ffdhe6144: return 259;
    case ossl_NID_ffdhe8192: return 260;
    //case ossl_NID_MLKEM512: return 512;
    //case ossl_NID_MLKEM768: return 513;
    //case ossl_NID_MLKEM1024: return 514;
    //case ossl_NID_SecP256r1MLKEM768: return 4587;
    //case ossl_NID_X25519MLKEM768: return 4588;
    //case ossl_NID_SecP384r1MLKEM1024: return 4589;
    //case ossl_NID_X25519Kyber768Draft00: return 25497;
    //case ossl_NID_SecP256r1Kyber768Draft00: return 25498;
    //case ossl_NID_arbitrary_explicit_prime_curves: return 65281;
    //case ossl_NID_arbitrary_explicit_char2_curves: return 65282;
    default: {
      if (nid | ossl_TLSEXT_nid_unknown) {
        return 0;
      }
      bssl_compat_error("Unknown negotiated group nid : %d", nid);
      return 0;
    }
  }
}
