#include "envoy/extensions/access_loggers/file/v3/file.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace {

class IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                        public HttpIntegrationTest {
public:
  IntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    useAccessLog("%METADATA(VIRTUAL_HOST:metadata.test:test_key)%");

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          ProtobufWkt::Struct struct_value;
          (*struct_value.mutable_fields())["test_key"] = ValueUtil::stringValue("test_value");

          (*hcm.mutable_route_config()
                ->mutable_virtual_hosts(0)
                ->mutable_metadata()
                ->mutable_filter_metadata())["metadata.test"] = struct_value;

          *hcm.mutable_route_config()
               ->mutable_virtual_hosts(0)
               ->mutable_routes(0)
               ->mutable_match()
               ->mutable_prefix() = "/expect";
        });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, IntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(IntegrationTest, RouteMatch) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, 0, "/expect");
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("test_value"));
}

TEST_P(IntegrationTest, RouteNoMatch) {
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());

  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("test_value"));
}

} // namespace
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
