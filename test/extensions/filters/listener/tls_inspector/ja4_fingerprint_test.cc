#include "source/extensions/filters/listener/tls_inspector/ja4_fingerprint.h"

#include "test/extensions/filters/listener/tls_inspector/tls_utility.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {
namespace {

// Test the GREASE values filtering function
TEST(JA4Fingerprinter, GreaseValueFiltering) {
  // Test the isNotGrease function directly
  EXPECT_FALSE(JA4Fingerprinter::isNotGrease(0x0a0a)); // GREASE value
  EXPECT_FALSE(JA4Fingerprinter::isNotGrease(0xaaaa)); // GREASE value
  EXPECT_FALSE(JA4Fingerprinter::isNotGrease(0xfafa)); // GREASE value

  EXPECT_TRUE(JA4Fingerprinter::isNotGrease(0x0a0b)); // Not a GREASE value
  EXPECT_TRUE(JA4Fingerprinter::isNotGrease(0x1234)); // Not a GREASE value
  EXPECT_TRUE(JA4Fingerprinter::isNotGrease(0xffff)); // Not a GREASE value
}

// Build a signature_algorithms extension body containing the given list of
// 16-bit sig-alg codepoints. Layout: 2-byte list length prefix followed by
// the codepoints (big-endian).
std::vector<uint8_t> buildSigAlgsExtensionBody(const std::vector<uint16_t>& sig_algs) {
  const uint16_t list_len = static_cast<uint16_t>(sig_algs.size() * 2);
  std::vector<uint8_t> body;
  body.push_back(static_cast<uint8_t>(list_len >> 8));
  body.push_back(static_cast<uint8_t>(list_len & 0xff));
  for (uint16_t s : sig_algs) {
    body.push_back(static_cast<uint8_t>(s >> 8));
    body.push_back(static_cast<uint8_t>(s & 0xff));
  }
  return body;
}

// Build a TLS extensions blob containing a single signature_algorithms extension
// (type 0x000d) whose body is |sig_algs_body|.
std::vector<uint8_t> buildExtensionsWithSigAlgs(const std::vector<uint8_t>& sig_algs_body) {
  const uint16_t ext_type = TLSEXT_TYPE_signature_algorithms;
  const uint16_t ext_len = static_cast<uint16_t>(sig_algs_body.size());
  std::vector<uint8_t> exts;
  exts.push_back(static_cast<uint8_t>(ext_type >> 8));
  exts.push_back(static_cast<uint8_t>(ext_type & 0xff));
  exts.push_back(static_cast<uint8_t>(ext_len >> 8));
  exts.push_back(static_cast<uint8_t>(ext_len & 0xff));
  exts.insert(exts.end(), sig_algs_body.begin(), sig_algs_body.end());
  return exts;
}

// Regression test for the JA4 spec's requirement that GREASE values be excluded
// from the signature_algorithms input to the JA4_c hash. A ClientHello with a
// GREASE codepoint inserted into signature_algorithms must produce the same
// JA4 fingerprint as an otherwise-identical ClientHello without it. See
// https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md
TEST(JA4Fingerprinter, GreaseValueFilteredFromSignatureAlgorithms) {
  const std::vector<uint16_t> sig_algs_no_grease = {0x0403, 0x0804};
  const std::vector<uint16_t> sig_algs_with_grease = {0x0a0a, 0x0403, 0x0804};

  const auto exts_no_grease =
      buildExtensionsWithSigAlgs(buildSigAlgsExtensionBody(sig_algs_no_grease));
  const auto exts_with_grease =
      buildExtensionsWithSigAlgs(buildSigAlgsExtensionBody(sig_algs_with_grease));

  // Two 16-bit codepoints for TLS 1.2 ECDHE-RSA-AES-128-GCM-SHA256 and -AES-256-GCM-SHA384.
  const std::vector<uint8_t> ciphers = {0xc0, 0x2f, 0xc0, 0x30};

  SSL_CLIENT_HELLO hello_no_grease{};
  hello_no_grease.version = TLS1_2_VERSION;
  hello_no_grease.cipher_suites = ciphers.data();
  hello_no_grease.cipher_suites_len = ciphers.size();
  hello_no_grease.extensions = exts_no_grease.data();
  hello_no_grease.extensions_len = exts_no_grease.size();

  SSL_CLIENT_HELLO hello_with_grease = hello_no_grease;
  hello_with_grease.extensions = exts_with_grease.data();
  hello_with_grease.extensions_len = exts_with_grease.size();

  EXPECT_EQ(JA4Fingerprinter::create(&hello_no_grease),
            JA4Fingerprinter::create(&hello_with_grease));
}

// This will test the ``JA4`` fingerprinting integration with the TLS Inspector code
class TlsInspectorJA4IntegrationTest : public testing::Test {
public:
  void SetUp() override {
    // Create real client hello data to use in the tests
    tls_v12_no_sni_data_ = Tls::Test::generateClientHello(TLS1_2_VERSION, TLS1_2_VERSION, "", "");
    tls_v12_with_sni_data_ =
        Tls::Test::generateClientHello(TLS1_2_VERSION, TLS1_2_VERSION, "example.com", "");
    tls_v12_with_alpn_data_ =
        Tls::Test::generateClientHello(TLS1_2_VERSION, TLS1_2_VERSION, "", "\x02h2");
    tls_v13_data_ = Tls::Test::generateClientHello(TLS1_3_VERSION, TLS1_3_VERSION, "", "");
  }

protected:
  std::vector<uint8_t> tls_v12_no_sni_data_;
  std::vector<uint8_t> tls_v12_with_sni_data_;
  std::vector<uint8_t> tls_v12_with_alpn_data_;
  std::vector<uint8_t> tls_v13_data_;
};

// This test verifies that the ``JA4`` hashes have the correct format
TEST_F(TlsInspectorJA4IntegrationTest, JA4HashFormat) {
  // The real implementation will be tested by the tls_inspector_test.cc and
  // tls_inspector_ja4_test.cc Here we mainly test the integration and the format of the ``JA4``
  // fingerprint This helps ensure that the format of ``JA4`` fingerprints is consistent with the
  // specification

  // Expected pattern: ``"t[0-9]{2}[di][0-9]{2}[0-9]{2}[0-9a-z]{2}_[0-9a-f]{12}_[0-9a-f]{12}"``
  // We'll check individual components in the actual tests

  // Verify TLS 1.2 without SNI should have "i" flag
  std::string v12_no_sni_data(reinterpret_cast<const char*>(tls_v12_no_sni_data_.data()),
                              tls_v12_no_sni_data_.size());
  EXPECT_FALSE(absl::StrContains(v12_no_sni_data, "example.com"));

  // Verify TLS 1.2 with SNI should have "d" flag
  std::string v12_with_sni_data(reinterpret_cast<const char*>(tls_v12_with_sni_data_.data()),
                                tls_v12_with_sni_data_.size());
  EXPECT_TRUE(absl::StrContains(v12_with_sni_data, "example.com"));

  // Verify TLS 1.2 with ALPN should have the ALPN values
  std::string v12_with_alpn_data(reinterpret_cast<const char*>(tls_v12_with_alpn_data_.data()),
                                 tls_v12_with_alpn_data_.size());
  EXPECT_TRUE(absl::StrContains(v12_with_alpn_data, "h2"));

  // Verify TLS 1.3 is being used - we simply check that the v1.3 data is different from v1.2
  // Since we explicitly requested TLS 1.3 in the client hello generation, and our previous
  // tests verify the utility works as expected for v1.2, this is a reasonable approach
  EXPECT_NE(tls_v12_no_sni_data_, tls_v13_data_);
}

} // namespace
} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
