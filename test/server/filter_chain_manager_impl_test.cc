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
#include "test/mocks/server/mocks.h"
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
  std::unique_ptr<Network::FilterChain>
  buildFilterChain(const envoy::config::listener::v3::FilterChain&,
                   FilterChainFactoryContextCreator&) const override {
    // Won't dereference but requires not nullptr.
    return std::make_unique<Network::MockFilterChain>();
  }
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

  void addSingleFilterChainHelper(const envoy::config::listener::v3::FilterChain& filter_chain) {
    filter_chain_manager_.addFilterChain(
        std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain},
        filter_chain_factory_builder_, filter_chain_manager_);
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
  envoy::config::listener::v3::FilterChain filter_chain_template_;
  MockFilterChainFactoryBuilder filter_chain_factory_builder_;
  NiceMock<Server::Configuration::MockFactoryContext> parent_context_;

  // Test target.
  FilterChainManagerImpl filter_chain_manager_{
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234), parent_context_};
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

// The current implementation generates independent contexts for the same filter chain
TEST_F(FilterChainManagerImplTest, FilterChainContextsAreUnique) {
  std::set<Configuration::FilterChainFactoryContext*> contexts;
  {
    for (int i = 0; i < 2; i++) {
      contexts.insert(
          &filter_chain_manager_.createFilterChainFactoryContext(&filter_chain_template_));
    }
  }
  EXPECT_EQ(contexts.size(), 2);
}

} // namespace Server
} // namespace Envoy
