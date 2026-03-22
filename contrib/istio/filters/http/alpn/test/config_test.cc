#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/istio/filters/http/alpn/source/alpn_filter.h"
#include "contrib/istio/filters/http/alpn/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

using istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig;

namespace Envoy {
namespace Http {
namespace Alpn {
namespace {

TEST(AlpnFilterConfigTest, OverrideAlpn) {
  const std::string yaml = R"EOF(
    alpn_override:
    - upstream_protocol: HTTP10
      alpn_override: ["foo", "bar"]
    - upstream_protocol: HTTP11
      alpn_override: ["baz"]
    - upstream_protocol: HTTP2
      alpn_override: ["qux"]
    )EOF";

  FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  AlpnConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  Http::StreamDecoderFilterSharedPtr added_filter;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_))
      .WillOnce(Invoke([&added_filter](Http::StreamDecoderFilterSharedPtr filter) {
        added_filter = std::move(filter);
      }));

  cb(filter_callback);
  EXPECT_NE(dynamic_cast<AlpnFilter*>(added_filter.get()), nullptr);
}

} // namespace
} // namespace Alpn
} // namespace Http
} // namespace Envoy
