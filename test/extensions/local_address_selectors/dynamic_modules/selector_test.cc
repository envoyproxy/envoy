#include <filesystem>

#include "envoy/extensions/local_address_selectors/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/extensions/local_address_selectors/dynamic_modules/config.h"
#include "source/extensions/local_address_selectors/dynamic_modules/selector.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace DynamicModules {
namespace {

using ::Envoy::Extensions::DynamicModules::failureCounter;
using ProtoConfig = envoy::extensions::local_address_selectors::dynamic_modules::v3::
    DynamicModuleLocalAddressSelector;

class DynamicModuleLocalAddressSelectorTest : public testing::Test {
public:
  DynamicModuleLocalAddressSelectorTest() {
    const std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("local_address_selector", "c");
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               std::filesystem::path(shared_object_path).parent_path().string(), 1);
  }

  ProtoConfig makeConfig(absl::string_view selector_name, absl::string_view selector_config = "",
                         absl::string_view module_name = "local_address_selector") {
    ProtoConfig config;
    config.mutable_dynamic_module_config()->set_name(module_name);
    config.mutable_dynamic_module_config()->set_do_not_close(true);
    config.set_selector_name(selector_name);
    if (!selector_config.empty()) {
      Protobuf::StringValue value;
      value.set_value(selector_config);
      std::ignore = config.mutable_selector_config()->PackFrom(value);
    }
    return config;
  }

  // One bind config entry. A null address stands for a bind config without a source_address.
  static std::vector<Upstream::UpstreamLocalAddress>
  bindAddresses(Network::Address::InstanceConstSharedPtr address) {
    return {{std::move(address), std::make_shared<Network::ConnectionSocket::Options>()}};
  }

  absl::StatusOr<Upstream::UpstreamLocalAddressSelectorConstSharedPtr>
  create(const ProtoConfig& config,
         std::vector<Upstream::UpstreamLocalAddress> addresses = bindAddresses(nullptr),
         std::optional<std::string> cluster_name = "cluster_0") {
    return factory_.createLocalAddressSelectorWithConfig(config, context_, std::move(addresses),
                                                         std::move(cluster_name));
  }

  static bool hasFailOption(const Network::ConnectionSocket::OptionsSharedPtr& options) {
    if (options == nullptr) {
      return false;
    }
    for (const auto& option : *options) {
      if (dynamic_cast<const FailConnectionSocketOption*>(option.get()) != nullptr) {
        return true;
      }
    }
    return false;
  }

  size_t countBindAddressNoPort(const Network::ConnectionSocket::OptionsSharedPtr& options) {
    size_t count = 0;
    for (const auto& option : *options) {
      const auto details =
          option->getOptionDetails(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);
      if (details.has_value() && details->name_ == ENVOY_SOCKET_IP_BIND_ADDRESS_NO_PORT) {
        ++count;
      }
    }
    return count;
  }

  DynamicModuleLocalAddressSelectorFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Network::MockConnectionSocket> socket_;
  const Network::Address::InstanceConstSharedPtr endpoint_ =
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1", 443);
};

TEST_F(DynamicModuleLocalAddressSelectorTest, FactoryName) {
  EXPECT_EQ("envoy.upstream.local_address_selector.dynamic_modules", factory_.name());
  EXPECT_NE(nullptr, dynamic_cast<ProtoConfig*>(factory_.createEmptyConfigProto().get()));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, RequiresConfig) {
  const auto selector = factory_.createLocalAddressSelector(bindAddresses(nullptr), "cluster_0");
  EXPECT_FALSE(selector.ok());
  EXPECT_THAT(selector.status().message(), testing::HasSubstr("requires its configuration"));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, UseDefault) {
  const auto bind_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.3", 0);
  const auto selector = create(makeConfig("default"), bindAddresses(bind_address));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  ASSERT_NE(nullptr, result.address_);
  EXPECT_EQ("127.0.0.3:0", result.address_->asString());
  EXPECT_FALSE(hasFailOption(result.socket_options_));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, UseSelected) {
  const auto selector = create(makeConfig("select", "127.0.0.2"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  ASSERT_NE(nullptr, result.address_);
  EXPECT_EQ("127.0.0.2:0", result.address_->asString());
  EXPECT_FALSE(hasFailOption(result.socket_options_));
}

// The module receives the endpoint address and port.
TEST_F(DynamicModuleLocalAddressSelectorTest, EndpointIsPassedToModule) {
  const auto selector = create(makeConfig("echo_endpoint"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  ASSERT_NE(nullptr, result.address_);
  EXPECT_EQ("10.0.0.1:444", result.address_->asString());

  const Network::Address::InstanceConstSharedPtr v6_endpoint =
      std::make_shared<Network::Address::Ipv6Instance>("2001:db8::1", 8080);
  const auto v6_result = selector.value()->getUpstreamLocalAddress(v6_endpoint, nullptr, {});
  ASSERT_NE(nullptr, v6_result.address_);
  EXPECT_EQ("[2001:db8::1]:8081", v6_result.address_->asString());
}

// A selected address with port 0 gets IP_BIND_ADDRESS_NO_PORT even when the bind config has no
// source_address, which is the case parseBindConfig() does not cover.
TEST_F(DynamicModuleLocalAddressSelectorTest, SelectedPortZeroBindsWithoutPort) {
  if (!ENVOY_SOCKET_IP_BIND_ADDRESS_NO_PORT.hasValue()) {
    GTEST_SKIP() << "IP_BIND_ADDRESS_NO_PORT is not supported on this platform";
  }
  const auto selector = create(makeConfig("select", "127.0.0.2"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  EXPECT_EQ(1, countBindAddressNoPort(result.socket_options_));
}

// The option is not added twice when the default address already carries it.
TEST_F(DynamicModuleLocalAddressSelectorTest, BindWithoutPortNotDuplicated) {
  if (!ENVOY_SOCKET_IP_BIND_ADDRESS_NO_PORT.hasValue()) {
    GTEST_SKIP() << "IP_BIND_ADDRESS_NO_PORT is not supported on this platform";
  }
  auto addresses = bindAddresses(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.3", 0));
  Network::Socket::appendOptions(addresses[0].socket_options_,
                                 Network::SocketOptionFactory::buildBindAddressNoPort());
  const auto selector = create(makeConfig("select", "127.0.0.2"), addresses);
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  EXPECT_EQ(1, countBindAddressNoPort(result.socket_options_));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, BindWithoutPortFollowsRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.upstream_bind_config_fix_port_exhaustion", "false"}});
  const auto selector = create(makeConfig("select", "127.0.0.2"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  EXPECT_EQ(0, countBindAddressNoPort(result.socket_options_));
}

// The connection's own socket options survive every result.
TEST_F(DynamicModuleLocalAddressSelectorTest, KeepsConnectionSocketOptions) {
  auto connection_options = std::make_shared<Network::ConnectionSocket::Options>();
  auto connection_option = std::make_shared<NiceMock<Network::MockSocketOption>>();
  connection_options->push_back(connection_option);
  const auto contains_connection_option =
      [&connection_option](const Network::ConnectionSocket::OptionsSharedPtr& options) {
        return std::find(options->begin(), options->end(), connection_option) != options->end();
      };

  for (const char* name : {"default", "select", "fail"}) {
    SCOPED_TRACE(name);
    const auto selector = create(makeConfig(name, "127.0.0.2"));
    ASSERT_TRUE(selector.ok()) << selector.status();
    const auto result =
        selector.value()->getUpstreamLocalAddress(endpoint_, connection_options, {});
    ASSERT_NE(nullptr, result.socket_options_);
    EXPECT_TRUE(contains_connection_option(result.socket_options_));
  }
}

TEST_F(DynamicModuleLocalAddressSelectorTest, Fail) {
  const auto selector = create(makeConfig("fail"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  EXPECT_EQ(nullptr, result.address_);
  EXPECT_TRUE(hasFailOption(result.socket_options_));
  // Applying the pre-bind options is what fails the client connection.
  EXPECT_FALSE(Network::Socket::applyOptions(result.socket_options_, socket_,
                                             envoy::config::core::v3::SocketOption::STATE_PREBIND));
  EXPECT_TRUE(Network::Socket::applyOptions(result.socket_options_, socket_,
                                            envoy::config::core::v3::SocketOption::STATE_BOUND));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, UseSelectedWithoutAddressFails) {
  const auto selector = create(makeConfig("no_address"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  EXPECT_EQ(nullptr, result.address_);
  EXPECT_TRUE(hasFailOption(result.socket_options_));
}

// The select mode fails the connection when set_address rejects the configured address.
TEST_F(DynamicModuleLocalAddressSelectorTest, InvalidSelectedAddressFails) {
  const auto selector = create(makeConfig("select", "not-an-ip"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  EXPECT_EQ(nullptr, result.address_);
  EXPECT_TRUE(hasFailOption(result.socket_options_));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, UnknownResultFails) {
  const auto selector = create(makeConfig("bad_result"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint_, nullptr, {});
  EXPECT_EQ(nullptr, result.address_);
  EXPECT_TRUE(hasFailOption(result.socket_options_));
}

// Envoy applies no source address to non-IP connections, so the module is not asked.
TEST_F(DynamicModuleLocalAddressSelectorTest, NonIpEndpointUsesDefault) {
  const auto selector = create(makeConfig("fail"));
  ASSERT_TRUE(selector.ok()) << selector.status();
  auto pipe_or_error = Network::Address::PipeInstance::create("/tmp/upstream.sock");
  ASSERT_TRUE(pipe_or_error.ok());
  const Network::Address::InstanceConstSharedPtr pipe = std::move(pipe_or_error.value());
  const auto result = selector.value()->getUpstreamLocalAddress(pipe, nullptr, {});
  EXPECT_FALSE(hasFailOption(result.socket_options_));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, ClusterNameIsPassedToModule) {
  EXPECT_TRUE(create(makeConfig("expect_cluster", "cluster_0")).ok());

  EXPECT_FALSE(
      create(makeConfig("expect_cluster", "cluster_0"), bindAddresses(nullptr), "cluster_1").ok());
  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "config_init_error", "expect_cluster"));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, ConfigNewRejected) {
  const auto selector = create(makeConfig("reject"));
  EXPECT_FALSE(selector.ok());
  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "config_init_error", "reject"));
  EXPECT_EQ(0U, failureCounter(context_.serverScope(), "module_load_error", "reject"));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, MissingSelectSymbol) {
  const auto selector = create(makeConfig("default", "", "local_address_selector_missing_select"));
  EXPECT_FALSE(selector.ok());
  EXPECT_THAT(selector.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_local_address_selector_select"));
  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "module_load_error", "default"));
}

TEST_F(DynamicModuleLocalAddressSelectorTest, ModuleNotFound) {
  EXPECT_FALSE(create(makeConfig("default", "", "does_not_exist")).ok());
}

// The bind config is validated before the module is loaded.
TEST_F(DynamicModuleLocalAddressSelectorTest, InvalidBindConfig) {
  std::vector<Upstream::UpstreamLocalAddress> addresses;
  for (const char* ip : {"127.0.0.1", "127.0.0.2", "127.0.0.3"}) {
    addresses.push_back({std::make_shared<Network::Address::Ipv4Instance>(ip, 0),
                         std::make_shared<Network::ConnectionSocket::Options>()});
  }
  EXPECT_FALSE(create(makeConfig("reject"), addresses).ok());
  EXPECT_EQ(0U, failureCounter(context_.serverScope(), "config_init_error", "reject"));
}

TEST(DynamicModuleLocalAddressSelectorAbiTest, SetAddress) {
  LocalAddressSelection selection;
  const std::string v4 = "192.0.2.10";
  EXPECT_TRUE(envoy_dynamic_module_callback_local_address_selector_set_address(
      &selection, {v4.data(), v4.size()}, 0));
  ASSERT_NE(nullptr, selection.selected_address_);
  EXPECT_EQ("192.0.2.10:0", selection.selected_address_->asString());

  const std::string v6 = "2001:db8::10";
  EXPECT_TRUE(envoy_dynamic_module_callback_local_address_selector_set_address(
      &selection, {v6.data(), v6.size()}, 1234));
  EXPECT_EQ("[2001:db8::10]:1234", selection.selected_address_->asString());
}

TEST(DynamicModuleLocalAddressSelectorAbiTest, SetAddressRejectsInvalidInput) {
  LocalAddressSelection selection;
  const std::string bracketed = "[2001:db8::10]";
  const std::string garbage = "not-an-ip";
  const std::string v4 = "192.0.2.10";
  EXPECT_FALSE(envoy_dynamic_module_callback_local_address_selector_set_address(
      &selection, {bracketed.data(), bracketed.size()}, 0));
  EXPECT_FALSE(envoy_dynamic_module_callback_local_address_selector_set_address(
      &selection, {garbage.data(), garbage.size()}, 0));
  EXPECT_FALSE(envoy_dynamic_module_callback_local_address_selector_set_address(
      &selection, {v4.data(), v4.size()}, 65536));
  EXPECT_FALSE(envoy_dynamic_module_callback_local_address_selector_set_address(&selection,
                                                                                {nullptr, 0}, 0));
  EXPECT_FALSE(envoy_dynamic_module_callback_local_address_selector_set_address(
      nullptr, {v4.data(), v4.size()}, 0));
  EXPECT_EQ(nullptr, selection.selected_address_);
}

} // namespace
} // namespace DynamicModules
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
