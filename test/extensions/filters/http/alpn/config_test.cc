#include "envoy/config/filter/http/alpn/v2alpha/alpn.pb.h"
#include "envoy/config/filter/http/alpn/v2alpha/alpn.pb.validate.h"

#include "extensions/filters/http/alpn/config.h"
#include "extensions/filters/http/alpn/alpn_filter.h"
#include "test/test_common/utility.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Alpn {
namespace {

TEST(AlpnFilterConfigTest, OverrideAlpn) {
  const std::string yaml = R"EOF(
    alpn_override:
    - downstream_protocol: HTTP10
      alpn: ["foo", "bar"]    
    - downstream_protocol: HTTP11
      alpn: ["baz"]
    - downstream_protocol: HTTP2
      alpn: ["qux"]
    )EOF";

  envoy::config::filter::http::alpn::v2alpha::FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  AlpnConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
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
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy