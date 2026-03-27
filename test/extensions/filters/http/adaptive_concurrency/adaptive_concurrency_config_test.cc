#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.validate.h"

#include "source/extensions/filters/http/adaptive_concurrency/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace {

TEST(AdaptiveConcurrencyConfigTest, AdaptiveConcurrencyFilter) {
  const std::string yaml = R"EOF(
gradient_controller_config:
  sample_aggregate_percentile:
    value: 50
  concurrency_limit_params:
    concurrency_update_interval: 0.1s
  min_rtt_calc_params:
    interval: 30s
    request_count: 50
enabled:
  default_value: true
  runtime_key: "adaptive_concurrency.enabled"
)EOF";

  AdaptiveConcurrencyFilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(testing::AnyNumber());
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(AdaptiveConcurrencyConfigTest, AdaptiveConcurrencyFilterSeverContext) {
  const std::string yaml = R"EOF(
gradient_controller_config:
  sample_aggregate_percentile:
    value: 50
  concurrency_limit_params:
    concurrency_update_interval: 0.1s
  min_rtt_calc_params:
    interval: 30s
    request_count: 50
enabled:
  default_value: true
  runtime_key: "adaptive_concurrency.enabled"
)EOF";

  AdaptiveConcurrencyFilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(testing::AnyNumber());
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProtoWithServerContext(
      *proto_config, "stats", context.server_factory_context_);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
