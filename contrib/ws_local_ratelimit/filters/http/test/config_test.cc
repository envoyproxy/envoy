#include "test/mocks/server/factory_context.h"

#include "contrib/envoy/extensions/filters/http/ws_local_ratelimit/v3alpha/ws_local_ratelimit.pb.h"
#include "contrib/ws_local_ratelimit/filters/http/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace WsLocalRateLimitFilter {
namespace {

TEST(WsLocalRateLimitFilterConfigTest, CreatesFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  WsLocalRateLimitFilterFactory factory;
  envoy::extensions::filters::http::ws_local_ratelimit::v3alpha::WsLocalRateLimit proto_config;
  proto_config.set_stat_prefix("test");
  proto_config.mutable_token_bucket()->set_max_tokens(5);
  proto_config.mutable_token_bucket()->mutable_tokens_per_fill()->set_value(5);
  *proto_config.mutable_token_bucket()->mutable_fill_interval() =
      Protobuf::util::TimeUtil::MillisecondsToDuration(10000);

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace WsLocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
