#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/stat_sinks/hystrix/config.h"
#include "extensions/stat_sinks/hystrix/hystrix.h"
#include "extensions/stat_sinks/well_known_names.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Hystrix {
namespace {

TEST(StatsConfigTest, ValidHystrixSink) {
  const std::string name = StatsSinkNames::get().Hystrix;

  envoy::config::metrics::v3::HystrixSink sink_config;

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Hystrix::HystrixSink*>(sink.get()), nullptr);
}

} // namespace
} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
