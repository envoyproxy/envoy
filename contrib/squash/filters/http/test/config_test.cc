#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/filters/http/squash/v3/squash.pb.h"
#include "contrib/envoy/extensions/filters/http/squash/v3/squash.pb.validate.h"
#include "contrib/squash/filters/http/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {
namespace {

TEST(SquashFilterConfigFactoryTest, SquashFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
  cluster: fake_cluster
  attachment_template:
    a: b
  request_timeout: 1.001s
  attachment_poll_period: 2.002s
  attachment_timeout: 3.003s
  )EOF";

  envoy::extensions::filters::http::squash::v3::Squash proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  SquashFilterConfigFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
