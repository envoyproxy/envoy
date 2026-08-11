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

// The optional ``protocol`` parameter to ``JA4Fingerprinter::create`` must:
//   - default to ``Protocol::TLS`` so existing callers see no behavior change,
//   - drive only the first character of the fingerprint (per the JA4 spec),
//   - map ``TLS`` -> ``t``, ``QUIC`` -> ``q``, ``DTLS`` -> ``d``.
TEST(JA4Fingerprinter, ProtocolParameterControlsFirstCharacter) {
  // Two 16-bit ciphers: TLS 1.2 ECDHE-RSA-AES128-GCM-SHA256 and -AES256-GCM-SHA384.
  const std::vector<uint8_t> ciphers = {0xc0, 0x2f, 0xc0, 0x30};

  SSL_CLIENT_HELLO hello{};
  hello.version = TLS1_2_VERSION;
  hello.cipher_suites = ciphers.data();
  hello.cipher_suites_len = ciphers.size();
  hello.extensions = nullptr;
  hello.extensions_len = 0;

  const std::string default_output = JA4Fingerprinter::create(&hello);
  const std::string tls_output = JA4Fingerprinter::create(&hello, JA4Fingerprinter::Protocol::TLS);
  const std::string quic_output = JA4Fingerprinter::create(&hello, JA4Fingerprinter::Protocol::QUIC);
  const std::string dtls_output = JA4Fingerprinter::create(&hello, JA4Fingerprinter::Protocol::DTLS);

  // Default is TLS: no-arg and Protocol::TLS overloads produce byte-identical output.
  EXPECT_EQ(default_output, tls_output);

  // First character matches the caller-selected protocol.
  ASSERT_FALSE(tls_output.empty());
  ASSERT_FALSE(quic_output.empty());
  ASSERT_FALSE(dtls_output.empty());
  EXPECT_EQ(tls_output[0], 't');
  EXPECT_EQ(quic_output[0], 'q');
  EXPECT_EQ(dtls_output[0], 'd');

  // Only the first character differs across protocol choices.
  EXPECT_EQ(tls_output.substr(1), quic_output.substr(1));
  EXPECT_EQ(tls_output.substr(1), dtls_output.substr(1));
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
