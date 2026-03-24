#include <chrono>
#include <string>

#include "source/common/common/base64.h"
#include "source/common/crypto/utility.h"
#include "source/extensions/filters/http/oauth2/client_assertion.h"

#include "test/mocks/common.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {
namespace {

// RSA private key for testing (PKCS#8 format).
const char RsaPrivateKeyPem[] = "-----BEGIN PRIVATE KEY-----\n"
                                "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDOeQHCllT34E4I\n"
                                "As9kEMnjVM4Lyq+m3iUh5FPw8/jAdgc4m7xqq6IuQb/1EkTQp7h9HScdJ9qY0Wsy\n"
                                "TQrOgLycI2wzwkqW5wCbTi5hjSRJEwQV5AAcwI5drKe1eU7WH+4dtb+Hmins4Owq\n"
                                "+SeBnlpcN+RcD8OuE63zmSgo5Nl9fXtb/XoGMYEvK63RumxviM/XZ+U9ZPR6xPYV\n"
                                "JeQ122JjVlcPHgL/DOTXu5K9hl7f0POXinzMBZwDSmBlz5F4Idrguach3xiLdEFR\n"
                                "zozCiWJbgYb2irpSkLjVaG2Lf2YjEyjbmkLVwDwkaFoJIqqeNNleZD4RVVWY1iDM\n"
                                "H3GFpdQXAgMBAAECggEATVzx1+NUOvyEwGOtKaVQwClKewibAD9EUoqnGSWREywm\n"
                                "UIOp+Z4Nyp9AOad6uWPesKJ3wWjpc1EkhVhwsCd0hFyRcmNeZ2RuycJlho/IBMln\n"
                                "QnyHvj44GclTnZ+ydnDIW8F53mlZRDSSyRdKQjr/SIZ4vjX58APXreq5LXlyNJ5f\n"
                                "Wk0h7Mnuy4EhMt/OxEd0VOCcB/UWhN9HIOBKniQ2LbjNIZbBgEeCpoIXS03Jd6hO\n"
                                "snwfZk8i62Szq65DPQRftO6jcwvE7zDQ+4WYRx3qLHj2VOvN7Yt0NhVcHwM2LoQJ\n"
                                "wGNgIrgRa87UxGCZxT+k2NjR9Pa+d3X9RI6oiERNoQKBgQDxH7iPhRQgLU47E3Jw\n"
                                "88uY2OF/ycr3fHbtqaG8DizrxMOZer+WvNuUW+7ePgHWRkkT9EbVlCGGd2GezbWE\n"
                                "tj3KgcrNn6kDCgDVuxQ0g7iqqGp9hhatwWZFN2yJBOJZ54Tl/O03E16o93aUDNM3\n"
                                "FVCs3Bry1Am/wa1yU6sVQVQP3QKBgQDbNgJRXBYLQYA9cyr7u49BH8AkZIky5E59\n"
                                "2Ocoy/57xSgqb1cCeWTIuiJhioPxFh0YclHv1d47t8g9UNtilbE5Lp6HwgV2GFja\n"
                                "7QVzF9gVyv5SJT6vL3Jol5Ze1G8KIS2DVaLS5kiC6eMhZsyn5DNsw7J5rOD2er7h\n"
                                "JuOQh2gugwKBgQDsCRtIEwOig/ciyWSrwVu6YgRMbaMsJUDeYcGbL1015sV6xrgp\n"
                                "vPJOBriMAbMWqHL8/5EfngQ7dz2ukLxyD1vpkqiOJQ7zlKVAlAOxbIgnNvoXql0k\n"
                                "9j9A3oJ2lrtlOsfTw4YK9gEh8iy3vN49+7WfoU8YCg0JE3TQh6rgAbViWQKBgBFW\n"
                                "GSLUFI45VOoHNKwJ7k9pMmnuZYdX1PlQ8R8h2vNw6TdJ7OiuLxFM3zE1oi+r3wsy\n"
                                "51X/ZP72DukCfwcx7X0nObRk3Me1LznJKvgqN5Wpoyld9rImH3c0HdlMFagIbbAI\n"
                                "UsM5IRzxYFwg5CiW/JYqd+/1gykbFgN6bt7cRpn/AoGBAMrcDL1OTwAwHjWUGQUp\n"
                                "yDJMGe13E4t1giiKIp+GxvJh+VuT1HoxiFazWF5osbkL5shGek6Pl/boIAZPjSeT\n"
                                "3fk+HPoRnx8WbeFdZYjZ6Kxf/TDJUzdMIlV9P4DSSYJCXf4AdUz6uBDI/xJq37CZ\n"
                                "ZNNg0dLTN88wdsU+TVn5Ef7u\n"
                                "-----END PRIVATE KEY-----";

using testing::NiceMock;
using testing::Return;

class ClientAssertionTest : public testing::Test {
public:
  ClientAssertionTest() { test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000))); }

  Event::SimulatedTimeSystem test_time_;
  NiceMock<Random::MockRandomGenerator> random_;
};

TEST_F(ClientAssertionTest, CreateRS256Assertion) {
  EXPECT_CALL(random_, uuid()).WillOnce(Return("test-jti-uuid"));

  auto result =
      ClientAssertion::create("my-client-id", "https://auth.example.com/token", RsaPrivateKeyPem,
                              "RS256", std::chrono::seconds(60), test_time_, random_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const std::string& jwt = result.value();

  // JWT should have three parts separated by dots.
  std::vector<std::string> parts = absl::StrSplit(jwt, '.');
  ASSERT_EQ(3, parts.size());

  // Decode and verify header.
  const std::string header_json = Base64Url::decode(parts[0]);
  EXPECT_NE(std::string::npos, header_json.find("\"alg\":\"RS256\""));
  EXPECT_NE(std::string::npos, header_json.find("\"typ\":\"JWT\""));

  // Decode and verify payload claims.
  const std::string payload_json = Base64Url::decode(parts[1]);
  EXPECT_NE(std::string::npos, payload_json.find("\"iss\":\"my-client-id\""));
  EXPECT_NE(std::string::npos, payload_json.find("\"sub\":\"my-client-id\""));
  EXPECT_NE(std::string::npos, payload_json.find("\"aud\":\"https://auth.example.com/token\""));
  EXPECT_NE(std::string::npos, payload_json.find("\"jti\":\"test-jti-uuid\""));
  // iat = 1000, exp = 1060
  EXPECT_NE(std::string::npos, payload_json.find("\"iat\":1000"));
  EXPECT_NE(std::string::npos, payload_json.find("\"exp\":1060"));

  // Signature should not be empty.
  EXPECT_FALSE(parts[2].empty());
}

TEST_F(ClientAssertionTest, CreateRS384Assertion) {
  EXPECT_CALL(random_, uuid()).WillOnce(Return("test-jti-uuid"));

  auto result =
      ClientAssertion::create("client", "https://auth.example.com/token", RsaPrivateKeyPem, "RS384",
                              std::chrono::seconds(120), test_time_, random_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  std::vector<std::string> parts = absl::StrSplit(result.value(), '.');
  ASSERT_EQ(3, parts.size());

  const std::string decoded_header = Base64Url::decode(parts[0]);
  EXPECT_NE(std::string::npos, decoded_header.find("\"alg\":\"RS384\""));

  const std::string payload_json = Base64Url::decode(parts[1]);
  // iat = 1000, exp = 1000 + 120 = 1120
  EXPECT_NE(std::string::npos, payload_json.find("\"exp\":1120"));
}

TEST_F(ClientAssertionTest, CreateRS512Assertion) {
  EXPECT_CALL(random_, uuid()).WillOnce(Return("test-jti-uuid"));

  auto result =
      ClientAssertion::create("client", "https://auth.example.com/token", RsaPrivateKeyPem, "RS512",
                              std::chrono::seconds(60), test_time_, random_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  std::vector<std::string> parts = absl::StrSplit(result.value(), '.');
  ASSERT_EQ(3, parts.size());

  const std::string decoded_header = Base64Url::decode(parts[0]);
  EXPECT_NE(std::string::npos, decoded_header.find("\"alg\":\"RS512\""));
}

TEST_F(ClientAssertionTest, UnsupportedAlgorithm) {
  auto result =
      ClientAssertion::create("client", "https://auth.example.com/token", RsaPrivateKeyPem, "PS256",
                              std::chrono::seconds(60), test_time_, random_);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(std::string::npos, result.status().message().find("Unsupported signing algorithm"));
}

TEST_F(ClientAssertionTest, InvalidPrivateKey) {
  // uuid() is never reached — key import fails first.
  auto result =
      ClientAssertion::create("client", "https://auth.example.com/token", "not-a-valid-pem-key",
                              "RS256", std::chrono::seconds(60), test_time_, random_);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(std::string::npos, result.status().message().find("Failed to parse private key"));
}

TEST_F(ClientAssertionTest, CustomLifetime) {
  EXPECT_CALL(random_, uuid()).WillOnce(Return("test-jti-uuid"));

  auto result =
      ClientAssertion::create("client", "https://auth.example.com/token", RsaPrivateKeyPem, "RS256",
                              std::chrono::seconds(300), test_time_, random_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  std::vector<std::string> parts = absl::StrSplit(result.value(), '.');
  ASSERT_EQ(3, parts.size());

  const std::string payload_json = Base64Url::decode(parts[1]);
  // iat = 1000, exp = 1000 + 300 = 1300
  EXPECT_NE(std::string::npos, payload_json.find("\"iat\":1000"));
  EXPECT_NE(std::string::npos, payload_json.find("\"exp\":1300"));
}

TEST_F(ClientAssertionTest, EmptyPrivateKey) {
  // uuid() is never reached — key import fails first.
  auto result = ClientAssertion::create("client", "https://auth.example.com/token", "", "RS256",
                                        std::chrono::seconds(60), test_time_, random_);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(std::string::npos, result.status().message().find("Failed to parse private key"));
}

TEST_F(ClientAssertionTest, SignatureIsVerifiable) {
  EXPECT_CALL(random_, uuid()).WillOnce(Return("test-jti-uuid"));

  auto result =
      ClientAssertion::create("client", "https://auth.example.com/token", RsaPrivateKeyPem, "RS256",
                              std::chrono::seconds(60), test_time_, random_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  // Extract the three JWT parts.
  std::vector<std::string> parts = absl::StrSplit(result.value(), '.');
  ASSERT_EQ(3, parts.size());

  // Reconstruct the signing input and decode the signature from the JWT.
  const std::string signing_input = absl::StrCat(parts[0], ".", parts[1]);
  const std::string raw_signature = Base64Url::decode(parts[2]);

  // Re-sign the same input with the same key and verify the signatures match.
  // RSA PKCS#1 v1.5 signing is deterministic, so identical inputs produce identical signatures.
  auto pkey = Common::Crypto::UtilitySingleton::get().importPrivateKeyPEM(RsaPrivateKeyPem);
  ASSERT_NE(nullptr, pkey);
  std::vector<uint8_t> text(signing_input.begin(), signing_input.end());
  auto sig_result = Common::Crypto::UtilitySingleton::get().sign("sha256", *pkey, text);
  ASSERT_TRUE(sig_result.ok());

  const std::string expected_sig(reinterpret_cast<const char*>(sig_result.value().data()),
                                 sig_result.value().size());
  EXPECT_EQ(expected_sig, raw_signature);
}

TEST_F(ClientAssertionTest, TwoAssertionsHaveDifferentJti) {
  EXPECT_CALL(random_, uuid()).WillOnce(Return("uuid-1")).WillOnce(Return("uuid-2"));

  auto result1 =
      ClientAssertion::create("client", "https://auth.example.com/token", RsaPrivateKeyPem, "RS256",
                              std::chrono::seconds(60), test_time_, random_);
  auto result2 =
      ClientAssertion::create("client", "https://auth.example.com/token", RsaPrivateKeyPem, "RS256",
                              std::chrono::seconds(60), test_time_, random_);
  ASSERT_TRUE(result1.ok());
  ASSERT_TRUE(result2.ok());

  // The two JWTs should differ because jti is different.
  EXPECT_NE(result1.value(), result2.value());

  // Verify the jti values in the payloads.
  std::vector<std::string> parts1 = absl::StrSplit(result1.value(), '.');
  std::vector<std::string> parts2 = absl::StrSplit(result2.value(), '.');
  const std::string payload1 = Base64Url::decode(parts1[1]);
  const std::string payload2 = Base64Url::decode(parts2[1]);
  EXPECT_NE(std::string::npos, payload1.find("\"jti\":\"uuid-1\""));
  EXPECT_NE(std::string::npos, payload2.find("\"jti\":\"uuid-2\""));
}

} // namespace
} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
