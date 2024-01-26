#include <iostream>
#include <ostream>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/listener_manager/filter_chain_manager_impl.h"
#include "source/common/network/socket_impl.h"

#include "test/benchmark/main.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

namespace {
class MockFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
  Network::DrainableFilterChainSharedPtr
  buildFilterChain(const envoy::config::listener::v3::FilterChain&,
                   FilterChainFactoryContextCreator&) const override {
    // A place holder to be found
    return std::make_shared<Network::MockFilterChain>();
  }
};

class MockConnectionSocket : public Network::ConnectionSocket {
public:
  MockConnectionSocket()
      : connection_info_provider_(
            std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr)) {}

  static std::unique_ptr<MockConnectionSocket>
  createMockConnectionSocket(uint16_t destination_port, const std::string& destination_address,
                             const std::string& server_name, const std::string& ja3_hash,
                             const std::string& transport_protocol,
                             const std::vector<std::string>& application_protocols,
                             const std::string& source_address, uint16_t source_port) {
    auto res = std::make_unique<MockConnectionSocket>();

    if (absl::StartsWith(destination_address, "/")) {
      res->connection_info_provider_->setLocalAddress(
          std::make_shared<Network::Address::PipeInstance>(destination_address));
    } else {
      res->connection_info_provider_->setLocalAddress(
          Network::Utility::parseInternetAddress(destination_address, destination_port));
    }
    if (absl::StartsWith(source_address, "/")) {
      res->connection_info_provider_->setRemoteAddress(
          std::make_shared<Network::Address::PipeInstance>(source_address));
    } else {
      res->connection_info_provider_->setRemoteAddress(
          Network::Utility::parseInternetAddress(source_address, source_port));
      res->connection_info_provider_->setDirectRemoteAddressForTest(
          Network::Utility::parseInternetAddress(source_address, source_port));
    }
    res->server_name_ = server_name;
    res->ja3_hash_ = ja3_hash;
    res->transport_protocol_ = transport_protocol;
    res->application_protocols_ = application_protocols;
    return res;
  }

  absl::string_view detectedTransportProtocol() const override { return transport_protocol_; }
  absl::string_view requestedServerName() const override { return server_name_; }
  absl::string_view ja3Hash() const override { return ja3_hash_; }
  const std::vector<std::string>& requestedApplicationProtocols() const override {
    return application_protocols_;
  }
  Network::ConnectionInfoSetter& connectionInfoProvider() override {
    return *connection_info_provider_;
  }
  const Network::ConnectionInfoSetter& connectionInfoProvider() const override {
    return *connection_info_provider_;
  }
  Network::ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr() const override {
    return connection_info_provider_;
  }

  // Wont call
  Network::IoHandle& ioHandle() override { return *io_handle_; }
  const Network::IoHandle& ioHandle() const override { return *io_handle_; }

  // Dummy method
  void close() override {}
  bool isOpen() const override { return false; }
  Network::Socket::Type socketType() const override { return Network::Socket::Type::Stream; }
  Network::Address::Type addressType() const override {
    return connection_info_provider_->localAddress()->type();
  }
  absl::optional<Network::Address::IpVersion> ipVersion() const override {
    return Network::Address::IpVersion::v4;
  }
  Network::SocketPtr duplicate() override { return nullptr; }
  void setDetectedTransportProtocol(absl::string_view) override {}
  void setRequestedApplicationProtocols(const std::vector<absl::string_view>&) override {}
  void addOption(const OptionConstSharedPtr&) override {}
  void addOptions(const OptionsSharedPtr&) override {}
  const OptionsSharedPtr& options() const override { return options_; }
  void setRequestedServerName(absl::string_view) override {}
  void setJA3Hash(absl::string_view) override {}
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr) override { return {0, 0}; }
  Api::SysCallIntResult listen(int) override { return {0, 0}; }
  Api::SysCallIntResult connect(const Network::Address::InstanceConstSharedPtr) override {
    return {0, 0};
  }
  Api::SysCallIntResult setSocketOption(int, int, const void*, socklen_t) override {
    return {0, 0};
  }
  Api::SysCallIntResult ioctl(unsigned long, void*, unsigned long, void*, unsigned long,
                              unsigned long*) override {
    return {0, 0};
  }
  Api::SysCallIntResult getSocketOption(int, int, void*, socklen_t*) const override {
    return {0, 0};
  }
  Api::SysCallIntResult setBlockingForTest(bool) override { return {0, 0}; }
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override { return {}; }
  absl::optional<uint64_t> congestionWindowInBytes() const override { return {}; }
  void dumpState(std::ostream&, int) const override {}

private:
  Network::IoHandlePtr io_handle_;
  OptionsSharedPtr options_;
  std::shared_ptr<Network::ConnectionInfoSetterImpl> connection_info_provider_;
  std::string server_name_;
  std::string ja3_hash_;
  std::string transport_protocol_;
  std::vector<std::string> application_protocols_;
};
const char YamlHeader[] = R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
    filter_chains:
    - filter_chain_match:
        # empty
      transport_socket:
        name: "envoy.transport_sockets.tls"
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext"
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a")EOF";
const char YamlSingleServer[] = R"EOF(
    - filter_chain_match:
        server_names: "server1.example.com"
        transport_protocol: "tls"
      transport_socket:
        name: "envoy.transport_sockets.tls"
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext"
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a")EOF";
const char YamlSingleDstPortTop[] = R"EOF(
    - filter_chain_match:
        destination_port: )EOF";
const char YamlSingleDstPortBottom[] = R"EOF(
      transport_socket:
        name: "envoy.transport_sockets.tls"
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext"
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a")EOF";
} // namespace

class FilterChainBenchmarkFixture : public ::benchmark::Fixture {
public:
  void initialize(::benchmark::State& state) {
    int64_t input_size = state.range(0);
    std::vector<std::string> port_chains;
    port_chains.reserve(input_size);
    for (int i = 0; i < input_size; i++) {
      port_chains.push_back(absl::StrCat(YamlSingleDstPortTop, 10000 + i, YamlSingleDstPortBottom));
    }
    listener_yaml_config_ = TestEnvironment::substitute(
        absl::StrCat(YamlHeader, YamlSingleServer, absl::StrJoin(port_chains, "")),
        Network::Address::IpVersion::v4);
    TestUtility::loadFromYaml(listener_yaml_config_, listener_config_);
    filter_chains_ = listener_config_.filter_chains();
  }

  Envoy::Thread::MutexBasicLockable lock_;
  Logger::Context logging_state_{spdlog::level::warn, Logger::Logger::DEFAULT_LOG_FORMAT, lock_,
                                 false};
  std::string listener_yaml_config_;
  envoy::config::listener::v3::Listener listener_config_;
  absl::Span<const envoy::config::listener::v3::FilterChain* const> filter_chains_;
  MockFilterChainFactoryBuilder dummy_builder_;
  Init::ManagerImpl init_manager_{"fcm_benchmark"};
};

// NOLINTNEXTLINE(readability-redundant-member-init)
BENCHMARK_DEFINE_F(FilterChainBenchmarkFixture, FilterChainManagerBuildTest)
(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks() && state.range(0) > 64) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  initialize(state);
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  addresses.emplace_back(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    FilterChainManagerImpl filter_chain_manager{addresses, factory_context, init_manager_};
    filter_chain_manager.addFilterChains(nullptr, filter_chains_, nullptr, dummy_builder_,
                                         filter_chain_manager);
  }
}

BENCHMARK_DEFINE_F(FilterChainBenchmarkFixture, FilterChainFindTest)
(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks() && state.range(0) > 64) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  initialize(state);
  std::vector<MockConnectionSocket> sockets;
  sockets.reserve(state.range(0));
  for (int i = 0; i < state.range(0); i++) {
    sockets.push_back(std::move(*MockConnectionSocket::createMockConnectionSocket(
        10000 + i, "127.0.0.1", "", "", "tls", {}, "8.8.8.8", 111)));
  }
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  addresses.emplace_back(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
  FilterChainManagerImpl filter_chain_manager{addresses, factory_context, init_manager_};

  filter_chain_manager.addFilterChains(nullptr, filter_chains_, nullptr, dummy_builder_,
                                       filter_chain_manager);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < state.range(0); i++) {
      filter_chain_manager.findFilterChain(sockets[i], stream_info);
    }
  }
}
BENCHMARK_REGISTER_F(FilterChainBenchmarkFixture, FilterChainManagerBuildTest)
    ->Ranges({
        // scale of the chains
        {1, 4096},
    })
    ->Unit(::benchmark::kMillisecond);
BENCHMARK_REGISTER_F(FilterChainBenchmarkFixture, FilterChainFindTest)
    ->Ranges({
        // scale of the chains
        {1, 4096},
    })
    ->Unit(::benchmark::kMillisecond);

/*
clang-format off

Run on (32 X 2200 MHz CPU s)
CPU Caches:
  L1 Data 32K (x16)
  L1 Instruction 32K (x16)
  L2 Unified 256K (x16)
  L3 Unified 56320K (x1)
Load Average: 19.05, 9.89, 3.92
-------------------------------------------------------------------------------------------------------
Benchmark                                                             Time             CPU   Iterations
-------------------------------------------------------------------------------------------------------
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/1        136994 ns       134510 ns         5183
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/8        583649 ns       574596 ns         1207
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/64      4483799 ns      4419618 ns          157
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/512    38864048 ns     38340468 ns           19
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/4096  318686843 ns    318568578 ns            2
FilterChainBenchmarkFixture/FilterChainFindTest/1                   201 ns          201 ns      3494470
FilterChainBenchmarkFixture/FilterChainFindTest/8                  1592 ns         1592 ns       435045
FilterChainBenchmarkFixture/FilterChainFindTest/64                16057 ns        16053 ns        44275
FilterChainBenchmarkFixture/FilterChainFindTest/512              172423 ns       172269 ns         4253
FilterChainBenchmarkFixture/FilterChainFindTest/4096            2676478 ns      2676167 ns          254

clang-format on
*/
} // namespace Server
} // namespace Envoy
