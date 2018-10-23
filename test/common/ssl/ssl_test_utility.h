#pragma once

#include <string>
#include <vector>

#include "test/test_common/environment.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

inline bssl::UniquePtr<X509> readCertFromFile(const std::string& path) {
  const std::string& file_content = TestEnvironment::readFileToStringForTest(path);
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file_content.c_str(), file_content.size()));
  bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  EXPECT_NE(cert, nullptr);
  return cert;
}

} // namespace Ssl
} // namespace Envoy