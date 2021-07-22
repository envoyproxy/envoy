#include <map>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_utils.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

TEST(AuthHelperTest, AuthMethod) {
  uint32_t cap = CLIENT_PROTOCOL_41;
  EXPECT_EQ(AuthHelper::authMethod(cap, "sha256_password"), AuthMethod::Sha256Password);
  EXPECT_EQ(AuthHelper::authMethod(cap, "mysql_old_password"), AuthMethod::OldPassword);
  EXPECT_EQ(AuthHelper::authMethod(cap, "caching_sha2_password"), AuthMethod::CacheSha2Password);
  EXPECT_EQ(AuthHelper::authMethod(cap, "mysql_native_password"), AuthMethod::NativePassword);
  EXPECT_EQ(AuthHelper::authMethod(cap, "mysql_clear_password"), AuthMethod::ClearPassword);
  EXPECT_EQ(AuthHelper::authMethod(cap, "mysql_unknown_password"), AuthMethod::Unknown);
  EXPECT_EQ(AuthHelper::authMethod(0, ""), AuthMethod::OldPassword);
  cap = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION | MYSQL_EXT_CL_PLUGIN_AUTH;
  EXPECT_EQ(AuthHelper::authMethod(cap, ""), AuthMethod::NativePassword);
}

// test cases come from https://github.com/go-sql-driver/mysql/blob/master/auth_test.go#L42
TEST(AuthHelperTest, OldPasswordSingature) {
  EXPECT_EQ(PasswordAuthHelper<OldPassword>::generateSeed().size(),
            PasswordAuthHelper<OldPassword>::seedLength());
  std::vector<uint8_t> seed = {9, 8, 7, 6, 5, 4, 3, 2};
  std::vector<std::pair<std::string, std::string>> test_cases = {
      {" pass", "47575c5a435b4251"},
      {"pass ", "47575c5a435b4251"},
      {"123\t456", "575c47505b5b5559"},
      {"C0mpl!ca ted#PASS123", "5d5d554849584a45"},
  };
  for (const auto& test_case : test_cases) {
    auto actual = PasswordAuthHelper<OldPassword>::signature(test_case.first, seed);
    EXPECT_EQ(Hex::encode(reinterpret_cast<uint8_t*>(actual.data()), actual.size()),
              test_case.second);
  }
}

// test cases come from https://github.com/go-sql-driver/mysql/blob/master/auth_test.go#L448
TEST(AuthHelperTest, NativePasswordSingature) {
  EXPECT_EQ(PasswordAuthHelper<NativePassword>::generateSeed().size(),
            PasswordAuthHelper<NativePassword>::seedLength());
  std::vector<uint8_t> seed = {70, 114, 92,  94,  1,  38, 11, 116, 63, 114,
                               23, 101, 126, 103, 26, 95, 81, 17,  24, 21};
  std::vector<std::pair<std::string, std::vector<uint8_t>>> test_cases = {
      {"secret", {53,  177, 140, 159, 251, 189, 127, 53, 109, 252,
                  172, 50,  211, 192, 240, 164, 26,  48, 207, 45}},
  };
  for (const auto& test_case : test_cases) {
    EXPECT_EQ(PasswordAuthHelper<NativePassword>::signature(test_case.first, seed),
              test_case.second);
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
