#include <gtest/gtest.h>
#include <openssl/x509v3.h>


TEST(X509V3Test, test_GENERAL_NAMES_new_free) {
  GENERAL_NAMES_free(GENERAL_NAMES_new());
}
