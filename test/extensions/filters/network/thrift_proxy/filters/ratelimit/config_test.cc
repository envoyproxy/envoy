#include "envoy/config/filter/thrift/rate_limit/v2alpha1/rate_limit.pb.validate.h"

#include "extensions/filters/common/ratelimit/ratelimit_registration.h"
#include "extensions/filters/network/thrift_proxy/filters/ratelimit/config.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace RateLimitFilter {

namespace {
envoy::config::filter::thrift::rate_limit::v2alpha1::RateLimit
parseRateLimitFromV2Yaml(const std::string& yaml) {
  envoy::config::filter::thrift::rate_limit::v2alpha1::RateLimit rate_limit;
  MessageUtil::loadFromYaml(yaml, rate_limit);
  return rate_limit;
}
} // namespace

TEST(RateLimitFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(
      RateLimitFilterConfig().createFilterFactoryFromProto(
          envoy::config::filter::thrift::rate_limit::v2alpha1::RateLimit(), "stats", context),
      ProtoValidationException);
}

TEST(RateLimitFilterConfigTest, RateLimitFilterCorrectProto) {
  std::string yaml_string = R"EOF(
domain: "test"
timeout: "1.337s"
  )EOF";

  auto proto_config = parseRateLimitFromV2Yaml(yaml_string);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;

  // Return the same singleton manager as instance so that config can be found there.
  EXPECT_CALL(context, singletonManager()).WillOnce(ReturnRef(instance.singletonManager()));

  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(
          instance, instance.clusterManager().grpcAsyncClientManager(),
          envoy::config::bootstrap::v2::Bootstrap());

  RateLimitFilterConfig factory;
  auto cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  NetworkFilters::ThriftProxy::ThriftFilters::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addDecoderFilter(_));
  cb(filter_callback);
}

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
