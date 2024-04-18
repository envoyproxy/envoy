#include "envoy/extensions/health_check/event_sinks/file/v3/file.pb.h"
#include "envoy/extensions/health_check/event_sinks/file/v3/file.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/health_check/event_sinks/file/file_sink_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::SaveArg;

namespace Envoy {
namespace Upstream {

TEST(HealthCheckEventFileSinkFactory, createHealthCheckEventSink) {
  auto factory = Envoy::Registry::FactoryRegistry<HealthCheckEventSinkFactory>::getFactory(
      "envoy.health_check.event_sink.file");
  EXPECT_NE(factory, nullptr);

  envoy::extensions::health_check::event_sinks::file::v3::HealthCheckEventFileSink config;
  config.set_event_log_path("test_path");
  Envoy::ProtobufWkt::Any typed_config;
  typed_config.PackFrom(config);

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;
  EXPECT_NE(factory->createHealthCheckEventSink(typed_config, context), nullptr);
}

TEST(HealthCheckEventFileSinkFactory, createEmptyHealthCheckEventSink) {
  auto factory = Envoy::Registry::FactoryRegistry<HealthCheckEventSinkFactory>::getFactory(
      "envoy.health_check.event_sink.file");
  EXPECT_NE(factory, nullptr);
  auto empty_proto = factory->createEmptyConfigProto();
  auto config = *dynamic_cast<
      envoy::extensions::health_check::event_sinks::file::v3::HealthCheckEventFileSink*>(
      empty_proto.get());
  EXPECT_TRUE(config.event_log_path().empty());
}

TEST(HealthCheckEventFileSink, logTest) {
  envoy::extensions::health_check::event_sinks::file::v3::HealthCheckEventFileSink config;
  config.set_event_log_path("test_path");
  NiceMock<AccessLog::MockAccessLogManager> log_manager;
  StringViewSaver file_log_data;
  ON_CALL(*log_manager.file_, write(_)).WillByDefault(SaveArg<0>(&file_log_data));

  HealthCheckEventFileSink file_sink(config, log_manager);

  envoy::data::core::v3::HealthCheckEvent event;
  TestUtility::loadFromYaml(R"EOF(
  health_checker_type: HTTP
  host:
    socket_address:
      protocol: TCP
      address: 10.0.0.1
      resolver_name: ''
      ipv4_compat: false
      port_value: 443
  cluster_name: fake_cluster
  eject_unhealthy_event:
    failure_type: ACTIVE
  timestamp: '2009-02-13T23:31:31.234Z'
  )EOF",
                            event);

  file_sink.log(event);
  EXPECT_EQ(
      file_log_data.value(),
      "{\"health_checker_type\":\"HTTP\",\"host\":{\"socket_address\":{"
      "\"protocol\":\"TCP\",\"address\":\"10.0.0.1\",\"port_value\":443,\"resolver_name\":\"\","
      "\"ipv4_compat\":false}},\"cluster_name\":\"fake_"
      "cluster\",\"eject_unhealthy_event\":{\"failure_type\":\"ACTIVE\"},"
      "\"timestamp\":\"2009-02-13T23:31:31.234Z\"}\n");

  envoy::data::core::v3::HealthCheckEvent add_event;
  TestUtility::loadFromYaml(R"EOF(
  health_checker_type: HTTP
  host:
    socket_address:
      protocol: TCP
      address: 10.0.0.1
      resolver_name: ''
      ipv4_compat: false
      port_value: 443
  cluster_name: fake_cluster
  add_healthy_event:
    first_check: false
  timestamp: '2009-02-13T23:31:31.234Z'
  )EOF",
                            add_event);

  file_sink.log(add_event);
  EXPECT_EQ(
      file_log_data.value(),
      "{\"health_checker_type\":\"HTTP\",\"host\":{\"socket_address\":{"
      "\"protocol\":\"TCP\",\"address\":\"10.0.0.1\",\"port_value\":443,\"resolver_name\":\"\","
      "\"ipv4_compat\":false}},\"cluster_name\":\"fake_"
      "cluster\",\"add_healthy_event\":{\"first_check\":false},\"timestamp\":"
      "\"2009-02-13T23:31:31.234Z\"}\n");
}

} // namespace Upstream
} // namespace Envoy
