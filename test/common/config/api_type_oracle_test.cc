#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"

#include "source/common/config/api_type_oracle.h"

#include "gtest/gtest.h"

// API_NO_BOOST_FILE

namespace Envoy {
namespace Config {
namespace {

TEST(ApiTypeOracleTest, All) {
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging v3_config;
  ProtobufWkt::Any non_api_type;

  EXPECT_EQ(nullptr,
            ApiTypeOracle::getEarlierVersionDescriptor(non_api_type.GetDescriptor()->full_name()));
  EXPECT_NE(envoy::extensions::filters::http::ip_tagging::v3::IPTagging::descriptor()->full_name(),
            ApiTypeOracle::getEarlierVersionMessageTypeName(v3_config.GetDescriptor()->full_name())
                .value());
}

} // namespace
} // namespace Config
} // namespace Envoy
