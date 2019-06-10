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
class FilterChainManagerImplTest : public testing::Test {
public:
  void SetUp() override {
    local_address_.reset(new Network::Address::Ipv4Instance("127.0.0.1", 1234));
    remote_address_.reset(new Network::Address::Ipv4Instance("127.0.0.1", 1234));
  }

  // Helper for test
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

  void addFilterChainHelper(
      uint16_t destination_port, const std::vector<std::string>& destination_ips,
      const std::vector<std::string>& server_names, const std::string& transport_protocol,
      const std::vector<std::string>& application_protocols,
      const envoy::api::v2::listener::FilterChainMatch_ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const Protobuf::RepeatedField<Protobuf::uint32>& source_ports) {
    Network::TransportSocketFactoryPtr dummy_mock_socketfactory{};
    std::vector<Network::FilterFactoryCb> dummy_filter_factory{};
    filter_chain_manager_.addFilterChain(
        destination_port, destination_ips, server_names, transport_protocol, application_protocols,
        source_type, source_ips, source_ports, std::move(dummy_mock_socketfactory),
        std::move(dummy_filter_factory));
  }

  // Intermedia state
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;

  std::vector<std::shared_ptr<Network::MockConnectionSocket>> sockets_;

  // Test target
  FilterChainManagerImpl filter_chain_manager_{
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234),
      ProtobufMessage::getNullValidationVisitor()};
};

TEST_F(FilterChainManagerImplTest, FilterChainMatchNothing) {
  auto filter_chain =
      findFilterChainHelper(0, "/tmp/test.sock", "", "tls", {}, "/tmp/test.sock", 111);
  EXPECT_EQ(filter_chain, nullptr);
}
} // namespace Server
} // namespace Envoy