#include "source/extensions/filters/udp/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

class DynamicModuleUdpListenerFilterFactoryTest : public testing::Test {
public:
  DynamicModuleUdpListenerFilterFactoryTest() {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("udp_no_op", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  DynamicModuleUdpListenerFilterConfigFactory factory_;
};

TEST_F(DynamicModuleUdpListenerFilterFactoryTest, ValidConfig) {
  NiceMock<Server::Configuration::MockListenerFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: udp_no_op
  do_not_close: true
filter_name: test_filter
filter_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: test_config
)EOF";

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto callback = factory_.createFilterFactoryFromProto(proto_config, context);

  NiceMock<Network::MockUdpListenerFilterManager> filter_manager;
  NiceMock<Network::MockUdpReadFilterCallbacks> read_callbacks;
  NiceMock<Event::MockDispatcher> worker_thread_dispatcher{"worker_0"};
  ON_CALL(read_callbacks.udp_listener_, dispatcher())
      .WillByDefault(testing::ReturnRef(worker_thread_dispatcher));

  EXPECT_CALL(filter_manager, addReadFilter_(testing::_));
  callback(filter_manager, read_callbacks);
}

TEST_F(DynamicModuleUdpListenerFilterFactoryTest, EmptyProto) {
  auto proto = factory_.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
}

TEST_F(DynamicModuleUdpListenerFilterFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.filters.udp_listener.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleUdpListenerFilterFactoryTest, InvalidModulePath) {
  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: nonexistent_module
filter_name: test_filter
)EOF";

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(proto_config, context),
                          EnvoyException, "Failed to load.*");
}

TEST_F(DynamicModuleUdpListenerFilterFactoryTest, ModuleWithoutUdpSupport) {
  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: no_op
filter_name: test_filter
)EOF";

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_MESSAGE(
      factory_.createFilterFactoryFromProto(proto_config, context), EnvoyException,
      "Dynamic module does not support UDP listener filters: Failed to "
      "resolve symbol envoy_dynamic_module_on_udp_listener_filter_config_new");
}

TEST_F(DynamicModuleUdpListenerFilterFactoryTest, MultipleFactoryCallsSameModule) {
  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: udp_no_op
  do_not_close: true
filter_name: test_filter
)EOF";

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto callback1 = factory_.createFilterFactoryFromProto(proto_config, context);
  auto callback2 = factory_.createFilterFactoryFromProto(proto_config, context);

  NiceMock<Network::MockUdpListenerFilterManager> filter_manager1;
  NiceMock<Network::MockUdpReadFilterCallbacks> read_callbacks1;
  NiceMock<Event::MockDispatcher> worker_thread_dispatcher{"worker_0"};
  ON_CALL(read_callbacks1.udp_listener_, dispatcher())
      .WillByDefault(testing::ReturnRef(worker_thread_dispatcher));
  EXPECT_CALL(filter_manager1, addReadFilter_(testing::_));
  callback1(filter_manager1, read_callbacks1);

  NiceMock<Network::MockUdpListenerFilterManager> filter_manager2;
  NiceMock<Network::MockUdpReadFilterCallbacks> read_callbacks2;
  ON_CALL(read_callbacks2.udp_listener_, dispatcher())
      .WillByDefault(testing::ReturnRef(worker_thread_dispatcher));
  EXPECT_CALL(filter_manager2, addReadFilter_(testing::_));
  callback2(filter_manager2, read_callbacks2);
}

TEST_F(DynamicModuleUdpListenerFilterFactoryTest, ConfigWithBytesValue) {
  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: udp_no_op
  do_not_close: true
filter_name: test_filter
filter_config:
  "@type": type.googleapis.com/google.protobuf.BytesValue
  value: "aGVsbG8="
)EOF";

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto callback = factory_.createFilterFactoryFromProto(proto_config, context);

  NiceMock<Network::MockUdpListenerFilterManager> filter_manager;
  NiceMock<Network::MockUdpReadFilterCallbacks> read_callbacks;
  NiceMock<Event::MockDispatcher> worker_thread_dispatcher{"worker_0"};
  ON_CALL(read_callbacks.udp_listener_, dispatcher())
      .WillByDefault(testing::ReturnRef(worker_thread_dispatcher));

  EXPECT_CALL(filter_manager, addReadFilter_(testing::_));
  callback(filter_manager, read_callbacks);
}

TEST_F(DynamicModuleUdpListenerFilterFactoryTest, ConfigWithStruct) {
  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: udp_no_op
  do_not_close: true
filter_name: test_filter
filter_config:
  "@type": type.googleapis.com/google.protobuf.Struct
  value:
    key1: value1
    key2: 123
)EOF";

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto callback = factory_.createFilterFactoryFromProto(proto_config, context);

  NiceMock<Network::MockUdpListenerFilterManager> filter_manager;
  NiceMock<Network::MockUdpReadFilterCallbacks> read_callbacks;
  NiceMock<Event::MockDispatcher> worker_thread_dispatcher{"worker_0"};
  ON_CALL(read_callbacks.udp_listener_, dispatcher())
      .WillByDefault(testing::ReturnRef(worker_thread_dispatcher));

  EXPECT_CALL(filter_manager, addReadFilter_(testing::_));
  callback(filter_manager, read_callbacks);
}

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
