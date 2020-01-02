#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3alpha/ip_tagging.pb.h"

#include "common/config/api_type_oracle.h"

#include "gtest/gtest.h"

// API_NO_BOOST_FILE

namespace Envoy {
namespace Config {
namespace {

TEST(ApiTypeOracleTest, All) {
  envoy::config::filter::http::ip_tagging::v2::IPTagging v2_config;
  envoy::extensions::filters::http::ip_tagging::v3alpha::IPTagging v3_config;
  ProtobufWkt::Any non_api_type;

  EXPECT_EQ(nullptr, ApiTypeOracle::getEarlierVersionDescriptor(non_api_type));
  EXPECT_EQ(nullptr, ApiTypeOracle::getEarlierVersionDescriptor(v2_config));
  const auto* desc = ApiTypeOracle::getEarlierVersionDescriptor(v3_config);
  EXPECT_EQ(envoy::config::filter::http::ip_tagging::v2::IPTagging::descriptor()->full_name(),
            desc->full_name());
}

} // namespace
} // namespace Config
} // namespace Envoy
