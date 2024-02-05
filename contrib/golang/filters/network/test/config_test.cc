#include <string>

#include "envoy/registry/registry.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "contrib/golang/filters/network/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {
namespace {

class MockThreadFactory : public Thread::ThreadFactory {
public:
  MOCK_METHOD(Thread::ThreadPtr, createThread, (std::function<void()>, Thread::OptionsOptConstRef));
  MOCK_METHOD(Thread::ThreadId, currentThreadId, ());
};

class GolangFilterConfigTestBase {
public:
  void testConfig(envoy::extensions::filters::network::golang::v3alpha::Config& config) {
    EXPECT_CALL(slot_allocator_, allocateSlot())
        .WillRepeatedly(Invoke(&slot_allocator_, &ThreadLocal::MockInstance::allocateSlotMock));
    ON_CALL(context_.server_factory_context_, threadLocal())
        .WillByDefault(ReturnRef(slot_allocator_));
    ON_CALL(context_.server_factory_context_.api_, threadFactory())
        .WillByDefault(ReturnRef(thread_factory_));

    Network::FilterFactoryCb cb;
    EXPECT_NO_THROW({ cb = factory_.createFilterFactoryFromProto(config, context_); });
    Network::MockConnection connection;
    EXPECT_CALL(connection, addFilter(_));
    cb(connection);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<MockThreadFactory> thread_factory_;
  ThreadLocal::MockInstance slot_allocator_;
  GolangConfigFactory factory_;
};

class GolangFilterConfigTest : public GolangFilterConfigTestBase, public testing::Test {
public:
  ~GolangFilterConfigTest() override = default;
};

TEST(GolangConfigFactoryTest, InvalidateEmptyConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      GolangConfigFactory().createFilterFactoryFromProto(
          envoy::extensions::filters::network::golang::v3alpha::Config(), context),
      Envoy::ProtoValidationException,
      "ConfigValidationError.LibraryId: value length must be at least 1 characters");
}

TEST_F(GolangFilterConfigTest, GolangFilterWithValidConfig) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  is_terminal_filter: true
  plugin_name: xxx
  plugin_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
    type_url: typexx
    value:
        key: value
        int: 10
  )EOF";

  auto yaml_string = absl::StrFormat(
      yaml_fmt, "test",
      TestEnvironment::substitute(
          "{{ test_rundir }}/contrib/golang/filters/network/test/test_data/filter.so"));
  envoy::extensions::filters::network::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  auto plugin_config = proto_config.plugin_config();
  std::string str;
  EXPECT_TRUE(plugin_config.SerializeToString(&str));

  testConfig(proto_config);
}

TEST_F(GolangFilterConfigTest, GolangFilterWithNilPluginConfig) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  plugin_name: xxx
  )EOF";

  auto yaml_string = absl::StrFormat(
      yaml_fmt, "test",
      TestEnvironment::substitute(
          "{{ test_rundir }}/contrib/golang/filters/network/test/test_data/filter.so"));
  envoy::extensions::filters::network::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  auto plugin_config = proto_config.plugin_config();
  std::string str;
  EXPECT_TRUE(plugin_config.SerializeToString(&str));

  testConfig(proto_config);
}

} // namespace
} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
