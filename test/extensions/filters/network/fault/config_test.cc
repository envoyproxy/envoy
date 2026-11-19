#include <string>

#include "envoy/extensions/filters/network/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/network/fault/v3/fault.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/fault/config.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Fault {
namespace {

TEST(NetworkFaultFilterConfigTest, RegisteredByWellKnownName) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedNetworkFilterConfigFactory>::getFactory(
          NetworkFilterNames::get().Fault);
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ("envoy.filters.network.fault", factory->name());
}

TEST(NetworkFaultFilterConfigTest, EmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  ProtobufTypes::MessagePtr empty_proto = factory.createEmptyConfigProto();
  ASSERT_NE(nullptr,
            Envoy::Protobuf::DynamicCastMessage<
                envoy::extensions::filters::network::fault::v3::NetworkFault>(empty_proto.get()));

  EXPECT_FALSE(factory.isTerminalFilterByProto(*empty_proto, context.serverFactoryContext()));

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*empty_proto, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFaultFilterConfigTest, FaultFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
stat_prefix: my_tcp_fault
abort_percent_runtime: custom.runtime.key
filter_metadata:
  experiment_name: "test_experiment"
  dry_run: true
abort:
  percentage:
    numerator: 50
    denominator: HUNDRED
  dry_run: true
)EOF";

  envoy::extensions::filters::network::fault::v3::NetworkFault proto_config;
  TestUtility::loadFromYamlAndValidate(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFaultFilterConfigTest, ProtocolOptionsNotSupported) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  envoy::extensions::filters::network::fault::v3::NetworkFault proto_config;
  auto status_or = factory.createProtocolOptionsConfig(proto_config, context);
  EXPECT_FALSE(status_or.ok());
}

} // namespace
} // namespace Fault
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
