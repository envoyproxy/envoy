#ifndef _IANA_2_OSSL_NAMES_H_
#define _IANA_2_OSSL_NAMES_H_

#include <string>


/**
 * Replaces all IANA cipher suite names with the equivalent OpenSSL names.
 * Anything that is not recognised as an IANA name is left unchanged.
 */
std::string iana_2_ossl_names(const char *str);

#endif //IANA_2_OSSL_NAMES_H_
