#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/config/metadata.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/socket_option_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

#include "server/configuration_impl.h"
#include "server/filter_chain_manager_impl.h"
#include "server/listener_manager_impl.h"

#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/drain_manager.h"
#include "test/mocks/server/factory_context.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

class MockFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
public:
  MockFilterChainFactoryBuilder() {
    ON_CALL(*this, buildFilterChain(_, _))
        .WillByDefault(Return(std::make_shared<Network::MockFilterChain>()));
  }

  MOCK_METHOD(Network::DrainableFilterChainSharedPtr, buildFilterChain,
              (const envoy::config::listener::v3::FilterChain&, FilterChainFactoryContextCreator&),
              (const));
};

class FilterChainManagerImplTest : public testing::Test {
public:
  void SetUp() override {
    local_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    TestUtility::loadFromYaml(
        TestEnvironment::substitute(filter_chain_yaml, Network::Address::IpVersion::v4),
        filter_chain_template_);
  }

  const Network::FilterChain*
  findFilterChainHelper(uint16_t destination_port, const std::string& destination_address,
                        const std::string& server_name, const std::string& transport_protocol,
                        const std::vector<std::string>& application_protocols,
                        const std::string& source_address, uint16_t source_port) {
    auto mock_socket = std::make_shared<NiceMock<Network::MockConnectionSocket>>();
    sockets_.push_back(mock_socket);

    if (absl::StartsWith(destination_address, "/")) {
      local_address_ = std::make_shared<Network::Address::PipeInstance>(destination_address);
    } else {
      local_address_ =
          Network::Utility::parseInternetAddress(destination_address, destination_port);
    }
    ON_CALL(*mock_socket, localAddress()).WillByDefault(ReturnRef(local_address_));

    ON_CALL(*mock_socket, requestedServerName())
        .WillByDefault(Return(absl::string_view(server_name)));
    ON_CALL(*mock_socket, detectedTransportProtocol())
        .WillByDefault(Return(absl::string_view(transport_protocol)));
    ON_CALL(*mock_socket, requestedApplicationProtocols())
        .WillByDefault(ReturnRef(application_protocols));

    if (absl::StartsWith(source_address, "/")) {
      remote_address_ = std::make_shared<Network::Address::PipeInstance>(source_address);
    } else {
      remote_address_ = Network::Utility::parseInternetAddress(source_address, source_port);
    }
    ON_CALL(*mock_socket, remoteAddress()).WillByDefault(ReturnRef(remote_address_));
    return filter_chain_manager_.findFilterChain(*mock_socket);
  }

  void addSingleFilterChainHelper(
      const envoy::config::listener::v3::FilterChain& filter_chain,
      const envoy::config::listener::v3::FilterChain* fallback_filter_chain = nullptr) {
    filter_chain_manager_.addFilterChains(
        std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain},
        fallback_filter_chain, filter_chain_factory_builder_, filter_chain_manager_);
  }

  // Intermediate states.
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  std::vector<std::shared_ptr<Network::MockConnectionSocket>> sockets_;

  // Reusable template.
  const std::string filter_chain_yaml = R"EOF(
      filter_chain_match:
        destination_port: 10000
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
  )EOF";
  Init::ManagerImpl init_manager_{"for_filter_chain_manager_test"};
  envoy::config::listener::v3::FilterChain filter_chain_template_;
  std::shared_ptr<Network::MockFilterChain> build_out_filter_chain_{
      std::make_shared<Network::MockFilterChain>()};
  envoy::config::listener::v3::FilterChain fallback_filter_chain_;
  std::shared_ptr<Network::MockFilterChain> build_out_fallback_filter_chain_{
      std::make_shared<Network::MockFilterChain>()};

  NiceMock<MockFilterChainFactoryBuilder> filter_chain_factory_builder_;
  NiceMock<Server::Configuration::MockFactoryContext> parent_context_;
  // Test target.
  FilterChainManagerImpl filter_chain_manager_{
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234), parent_context_,
      init_manager_};
};

TEST_F(FilterChainManagerImplTest, FilterChainMatchNothing) {
  auto filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);
}

TEST_F(FilterChainManagerImplTest, AddSingleFilterChain) {
  addSingleFilterChainHelper(filter_chain_template_);
  auto* filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain, nullptr);
}

TEST_F(FilterChainManagerImplTest, FilterChainUseFallbackIfNoFilterChainMatches) {
  // The build helper will build matchable filter chain and then build the default filter chain.
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _))
      .WillOnce(Return(build_out_fallback_filter_chain_));
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _))
      .WillOnce(Return(std::make_shared<Network::MockFilterChain>()))
      .RetiresOnSaturation();
  addSingleFilterChainHelper(filter_chain_template_, &fallback_filter_chain_);

  auto filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain, nullptr);
  auto fallback_filter_chain =
      findFilterChainHelper(9999, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(fallback_filter_chain, build_out_fallback_filter_chain_.get());
}

TEST_F(FilterChainManagerImplTest, LookupFilterChainContextByFilterChainMessage) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;

  for (int i = 0; i < 2; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check.
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _)).Times(2);
  filter_chain_manager_.addFilterChains(
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_messages[0],
                                                                   &filter_chain_messages[1]},
      nullptr, filter_chain_factory_builder_, filter_chain_manager_);
}

TEST_F(FilterChainManagerImplTest, DuplicateContextsAreNotBuilt) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;

  for (int i = 0; i < 3; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }

  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _)).Times(1);
  filter_chain_manager_.addFilterChains(
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_messages[0]},
      nullptr, filter_chain_factory_builder_, filter_chain_manager_);

  FilterChainManagerImpl new_filter_chain_manager{
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234), parent_context_,
      init_manager_, filter_chain_manager_};
  // The new filter chain manager maintains 3 filter chains, but only 2 filter chain context is
  // built because it reuse the filter chain context in the previous filter chain manager
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _)).Times(2);
  new_filter_chain_manager.addFilterChains(
      std::vector<const envoy::config::listener::v3::FilterChain*>{
          &filter_chain_messages[0], &filter_chain_messages[1], &filter_chain_messages[2]},
      nullptr, filter_chain_factory_builder_, new_filter_chain_manager);
}

TEST_F(FilterChainManagerImplTest, CreatedFilterChainFactoryContextHasIndependentDrainClose) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;
  for (int i = 0; i < 3; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }
  auto context0 = filter_chain_manager_.createFilterChainFactoryContext(&filter_chain_messages[0]);
  auto context1 = filter_chain_manager_.createFilterChainFactoryContext(&filter_chain_messages[1]);

  // Server as whole is not draining.
  MockDrainManager not_a_draining_manager;
  EXPECT_CALL(not_a_draining_manager, drainClose).WillRepeatedly(Return(false));
  Configuration::MockServerFactoryContext mock_server_context;
  EXPECT_CALL(mock_server_context, drainManager).WillRepeatedly(ReturnRef(not_a_draining_manager));
  EXPECT_CALL(parent_context_, getServerFactoryContext)
      .WillRepeatedly(ReturnRef(mock_server_context));

  EXPECT_FALSE(context0->drainDecision().drainClose());
  EXPECT_FALSE(context1->drainDecision().drainClose());

  // Drain filter chain 0
  auto* context_impl_0 = dynamic_cast<PerFilterChainFactoryContextImpl*>(context0.get());
  context_impl_0->startDraining();

  EXPECT_TRUE(context0->drainDecision().drainClose());
  EXPECT_FALSE(context1->drainDecision().drainClose());
}
} // namespace Server
} // namespace Envoy
