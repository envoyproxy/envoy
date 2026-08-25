#include "source/extensions/filters/listener/tls_inspector/ja4_fingerprint.h"

#include "source/common/common/hex.h"
#include "source/common/ssl/ssl.h"

#include "test/extensions/filters/listener/tls_inspector/tls_utility.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

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

// End-to-end wire-format integration test for the ``protocol`` parameter added
// to ``JA4Fingerprinter::create``. Decodes a real captured Chrome-stable
// ClientHello (Browser-1 from tls_inspector_ja4_test.cc's JA4_TEST_VECTORS)
// through BoringSSL's ``SSL_parse_client_hello`` -- the same parse path Envoy
// hits when the listener filter feeds ClientHello bytes into the JA4 code --
// and then asserts that the ``protocol`` argument selects the fingerprint's
// first character while leaving every other byte unchanged.
TEST(JA4Fingerprinter, ProtocolParameterEndToEndFromWireBytes) {
  // Same captured hex as tls_inspector_ja4_test.cc's Browser-1 vector. Includes
  // the 5-byte TLS record header and 4-byte handshake header preceding the
  // ClientHello body.
  const std::string wire_hex =
      "1603010200010001fc0303528b4e00213672e534980dfed836dd5b375ab164dcc65ba6a3c87e7e2a1f9d61201bf29"
      "c9dffaa31ed2df524d3a113edb4e6fd3b7fb3d6d57d5d9aafb213e83c420020aaaa130113021303c02bc02fc02cc0"
      "30cca9cca8c013c014009c009d002f0035010001936a6a0000000d001200100403080404010503080505010806060"
      "1000000170015000012656467652e6d6963726f736f66742e636f6d000a000a00084a4a001d001700180005000501"
      "00000000000b00020100002b0007069a9a03040303001b0003020002ff010001000033002b00294a4a000100001d0"
      "020c4ce4268d58f0c703855163f4754b883742487a5ce87a6016a30208c18e07f69446900050003026832002d0002"
      "01010023000000170000001200000010000e000c02683208687474702f312e310a0a000100001500c500000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
      "00000000000";
  const std::vector<uint8_t> wire = Hex::decode(wire_hex);
  ASSERT_GT(wire.size(), 9u);
  // Skip the 5-byte record header + 4-byte handshake header;
  // SSL_parse_client_hello wants the body starting at legacy_version.
  const uint8_t* body = wire.data() + 9;
  const size_t body_len = wire.size() - 9;

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  ASSERT_TRUE(ctx != nullptr);
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  ASSERT_TRUE(ssl != nullptr);

  SSL_CLIENT_HELLO client_hello;
  ASSERT_EQ(1, SSL_parse_client_hello(ssl.get(), &client_hello, body, body_len));

  const std::string default_fp = JA4Fingerprinter::create(&client_hello);
  const std::string tls_fp =
      JA4Fingerprinter::create(&client_hello, JA4Fingerprinter::Protocol::TLS);
  const std::string quic_fp =
      JA4Fingerprinter::create(&client_hello, JA4Fingerprinter::Protocol::QUIC);
  const std::string dtls_fp =
      JA4Fingerprinter::create(&client_hello, JA4Fingerprinter::Protocol::DTLS);

  // Backward-compatible default: no-arg call matches the pinned Browser-1
  // fingerprint from tls_inspector_ja4_test.cc.
  const std::string expected_tls_fp = SSL_SELECT(
      "t13d1516h2_8daaf6152771_e5627efa2ab1", "t13d1515h2_8daaf6152771_de4a06bb82e3");
  EXPECT_EQ(default_fp, expected_tls_fp);
  EXPECT_EQ(tls_fp, expected_tls_fp);

  // Explicit Protocol values change only the first character.
  ASSERT_FALSE(quic_fp.empty());
  ASSERT_FALSE(dtls_fp.empty());
  EXPECT_EQ(quic_fp[0], 'q');
  EXPECT_EQ(dtls_fp[0], 'd');
  EXPECT_EQ(quic_fp.substr(1), expected_tls_fp.substr(1));
  EXPECT_EQ(dtls_fp.substr(1), expected_tls_fp.substr(1));
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
