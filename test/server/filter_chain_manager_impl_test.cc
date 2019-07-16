#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/admin/v2alpha/config_dump.pb.h"
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

#include "extensions/filters/listener/original_dst/original_dst.h"
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

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::Throw;

namespace Envoy {
namespace Server {

MATCHER_P(containsServerName, name, "") {
  const auto& names = arg.filter_chain_match().server_names();
  return std::find(names.begin(), names.end(), name) != std::end(names);
}

class MockFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
public:
  MockFilterChainFactoryBuilder() {
    EXPECT_CALL(*this, buildFilterChain(_)).WillRepeatedly(Invoke([](auto) {
      return std::make_unique<Network::MockFilterChain>();
    }));
  }

  MOCK_CONST_METHOD1(buildFilterChain, std::unique_ptr<Network::FilterChain>(
                                           const ::envoy::api::v2::listener::FilterChain&));
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

  void addSingleFilterChainHelper(const envoy::api::v2::listener::FilterChain& filter_chain) {
    filter_chain_manager_.addFilterChain(
        std::vector<const envoy::api::v2::listener::FilterChain*>{&filter_chain},
        filter_chain_factory_builder_);
  }

  // Intermedia states.
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  std::vector<std::shared_ptr<Network::MockConnectionSocket>> sockets_;

  // Reuseable template.
  const std::string filter_chain_yaml = R"EOF(
      filter_chain_match:
        destination_port: 10000
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
  )EOF";
  envoy::api::v2::listener::FilterChain filter_chain_template_;
  MockFilterChainFactoryBuilder filter_chain_factory_builder_;

  // Test target.
  FilterChainManagerImpl filter_chain_manager_{
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234)};
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

TEST_F(FilterChainManagerImplTest, EmptyDstAddressAndCatchAllAddress) {
  const std::string filter_chain_yaml0 = R"EOF(
      filter_chain_match:
          destination_port: 443
          server_names:
          - "*.foo.com"
      filters:
  )EOF";
  const std::string filter_chain_yaml1 = R"EOF(
      filter_chain_match:
          destination_port: 443
          prefix_ranges:
          - address_prefix: 0.0.0.0
            prefix_len: 0
          server_names:
            - "*.bar.com"  
      filters:
  )EOF";

  envoy::api::v2::listener::FilterChain filter_chain_0, filter_chain_1;
  TestUtility::loadFromYaml(
      TestEnvironment::substitute(filter_chain_yaml0, Network::Address::IpVersion::v4),
      filter_chain_0);
  TestUtility::loadFromYaml(
      TestEnvironment::substitute(filter_chain_yaml1, Network::Address::IpVersion::v4),
      filter_chain_1);
  std::vector<Network::FilterFactoryCb> ref0, ref1;

  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(containsServerName("*.foo.com")))
      .WillRepeatedly(Invoke([&ref0](auto) {
        auto res = std::make_unique<Network::MockFilterChain>();
        EXPECT_CALL(*res, networkFilterFactories()).Times(1).WillRepeatedly(ReturnRef(ref0));
        return res;
      }));
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(containsServerName("*.bar.com")))
      .WillRepeatedly(Invoke([&ref1](auto) {
        auto res = std::make_unique<Network::MockFilterChain>();
        EXPECT_CALL(*res, networkFilterFactories()).Times(1).WillRepeatedly(ReturnRef(ref1));
        return res;
      }));
  filter_chain_manager_.addFilterChain(
      std::vector<const envoy::api::v2::listener::FilterChain*>{&filter_chain_0, &filter_chain_1},
      filter_chain_factory_builder_);

  auto* filter_chain =
      findFilterChainHelper(443, "127.0.0.1", "www.foo.com", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain, nullptr);
  EXPECT_EQ(&ref0, &filter_chain->networkFilterFactories());

  filter_chain = findFilterChainHelper(443, "127.0.0.1", "www.bar.com", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain, nullptr);
  EXPECT_EQ(&ref1, &filter_chain->networkFilterFactories());
}

TEST_F(FilterChainManagerImplTest, EmptyDstAddressIsSuperSetOfCatchAllAddress) {
  const std::string filter_chain_yaml0 = R"EOF(
      filter_chain_match:
          destination_port: 443
          server_names:
          - "*.foo.com"
      filters:
  )EOF";
  const std::string filter_chain_yaml1 = R"EOF(
      filter_chain_match:
          destination_port: 443
          prefix_ranges:
          - address_prefix: 0.0.0.0
            prefix_len: 0
          server_names:
            - "*.foo.com"  
      filters:
  )EOF";

  envoy::api::v2::listener::FilterChain filter_chain_0, filter_chain_1;
  TestUtility::loadFromYaml(
      TestEnvironment::substitute(filter_chain_yaml0, Network::Address::IpVersion::v4),
      filter_chain_0);
  TestUtility::loadFromYaml(
      TestEnvironment::substitute(filter_chain_yaml1, Network::Address::IpVersion::v4),
      filter_chain_1);
  EXPECT_THROW_WITH_MESSAGE(filter_chain_manager_.addFilterChain(
                                std::vector<const envoy::api::v2::listener::FilterChain*>{
                                    &filter_chain_0, &filter_chain_1},
                                filter_chain_factory_builder_),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': multiple filter chains with "
                            "overlapping matching rules are defined");
}
} // namespace Server
} // namespace Envoy