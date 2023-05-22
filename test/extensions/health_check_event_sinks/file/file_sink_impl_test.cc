#include "envoy/extensions/health_check_event_sinks/file/v3/file.pb.h"
#include "envoy/extensions/health_check_event_sinks/file/v3/file.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/health_check_event_sinks/file/file_sink_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

TEST(HealthCheckEventFileSinkFactory, createHealthCheckEventSink) {
  auto factory = Envoy::Registry::FactoryRegistry<HealthCheckEventSinkFactory>::getFactory(
      "envoy.health_check_event_sink.file");
  EXPECT_NE(factory, nullptr);

  ::envoy::extensions::health_check_event_sinks::file::v3::HealthCheckEventFileSink config;
  config.set_event_log_path("test_path");
  Envoy::ProtobufWkt::Any typed_config;
  typed_config.PackFrom(config);

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;
  EXPECT_NE(factory->createHealthCheckEventSink(typed_config, context), nullptr);
}

} // namespace Upstream
} // namespace Envoy
