#include <iostream>
#include <unordered_map>

#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/protobuf/message_validator.h"

#include "server/filter_chain_manager_impl.h"

#include "extensions/transport_sockets/well_known_names.h"

#include "test/mocks/network/mocks.h"
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
  std::unique_ptr<Network::FilterChain>
  buildFilterChain(const ::envoy::api::v2::listener::FilterChain&) const override {
    // A place holder to be found
    return std::make_unique<Network::MockFilterChain>();
  }
};

class MockConnectionSocket : public Network::ConnectionSocket {
public:
  MockConnectionSocket() = default;
  static std::unique_ptr<MockConnectionSocket>
  createMockConnectionSocket(uint16_t destination_port, const std::string& destination_address,
                             const std::string& server_name, const std::string& transport_protocol,
                             const std::vector<std::string>& application_protocols,
                             const std::string& source_address, uint16_t source_port) {
    auto res = std::make_unique<MockConnectionSocket>();

    if (absl::StartsWith(destination_address, "/")) {
      res->local_address_ = std::make_shared<Network::Address::PipeInstance>(destination_address);
    } else {
      res->local_address_ =
          Network::Utility::parseInternetAddress(destination_address, destination_port);
    }
    if (absl::StartsWith(source_address, "/")) {
      res->remote_address_ = std::make_shared<Network::Address::PipeInstance>(source_address);
    } else {
      res->remote_address_ = Network::Utility::parseInternetAddress(source_address, source_port);
    }
    res->server_name_ = server_name;
    res->transport_protocol_ = transport_protocol;
    res->application_protocols_ = application_protocols;
    return res;
  }

  const Network::Address::InstanceConstSharedPtr& remoteAddress() const override {
    return remote_address_;
  }

  const Network::Address::InstanceConstSharedPtr& localAddress() const override {
    return local_address_;
  }

  absl::string_view detectedTransportProtocol() const override { return transport_protocol_; }

  absl::string_view requestedServerName() const override { return server_name_; }
  const std::vector<std::string>& requestedApplicationProtocols() const override {
    return application_protocols_;
  }

  // Wont call
  Network::IoHandle& ioHandle() override { return *io_handle_; }
  const Network::IoHandle& ioHandle() const override { return *io_handle_; }

  // Dummy method
  void close() override {}
  Network::Address::SocketType socketType() const override {
    return Network::Address::SocketType::Stream;
  }
  void setLocalAddress(const Network::Address::InstanceConstSharedPtr&) override {}
  void restoreLocalAddress(const Network::Address::InstanceConstSharedPtr&) override { return; }
  void setRemoteAddress(const Network::Address::InstanceConstSharedPtr&) override {}
  bool localAddressRestored() const override { return true; }
  void setDetectedTransportProtocol(absl::string_view) override {}
  void setRequestedApplicationProtocols(const std::vector<absl::string_view>&) override {}
  void addOption(const OptionConstSharedPtr&) override {}
  void addOptions(const OptionsSharedPtr&) override {}
  const OptionsSharedPtr& options() const override { return options_; }
  void setRequestedServerName(absl::string_view) override {}

private:
  Network::IoHandlePtr io_handle_;
  OptionsSharedPtr options_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  std::string server_name_;
  std::string transport_protocol_;
  std::vector<std::string> application_protocols_;
};
const std::string yaml_header = R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        # empty
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a")EOF";
const std::string yaml_single_server = R"EOF(
    - filter_chain_match:
        server_names: "server1.example.com"
        transport_protocol: "tls"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a")EOF";
const std::string yaml_single_dst_port_top = R"EOF(
    - filter_chain_match:
        destination_port: )EOF";
const std::string yaml_single_dst_port_bottom = R"EOF(
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a")EOF";
} // namespace

class FilterChainBenchmarkFixture : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State& state) {
    int64_t input_size = state.range(0);
    std::vector<std::string> port_chains;
    for (int i = 0; i < input_size; i++) {
      port_chains.push_back(
          absl::StrCat(yaml_single_dst_port_top, 10000 + i, yaml_single_dst_port_bottom));
    }
    listener_yaml_config_ = TestEnvironment::substitute(
        absl::StrCat(yaml_header, yaml_single_server, absl::StrJoin(port_chains, "")),
        Network::Address::IpVersion::v4);
    TestUtility::loadFromYaml(listener_yaml_config_, listener_config_);
    filter_chains_ = listener_config_.filter_chains();
  }
  absl::Span<const envoy::api::v2::listener::FilterChain* const> filter_chains_;
  std::string listener_yaml_config_;
  envoy::api::v2::Listener listener_config_;
  MockFilterChainFactoryBuilder dummy_builder_;
  FilterChainManagerImpl filter_chain_manager_{
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234)};
};

BENCHMARK_DEFINE_F(FilterChainBenchmarkFixture, FilterChainManagerBuildTest)
(::benchmark::State& state) {
  for (auto _ : state) {
    FilterChainManagerImpl filter_chain_manager{
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234)};
    filter_chain_manager.addFilterChain(filter_chains_, dummy_builder_);
  }
}

BENCHMARK_DEFINE_F(FilterChainBenchmarkFixture, FilterChainFindTest)
(::benchmark::State& state) {
  std::vector<MockConnectionSocket> sockets;
  sockets.reserve(state.range(0));
  for (int i = 0; i < state.range(0); i++) {
    sockets.push_back(std::move(*MockConnectionSocket::createMockConnectionSocket(
        10000 + i, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111)));
  }
  FilterChainManagerImpl filter_chain_manager{
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234)};
  filter_chain_manager.addFilterChain(filter_chains_, dummy_builder_);
  for (auto _ : state) {
    for (int i = 0; i < state.range(0); i++) {
      filter_chain_manager.findFilterChain(sockets[i]);
    }
  }
}
BENCHMARK_REGISTER_F(FilterChainBenchmarkFixture, FilterChainManagerBuildTest)
    ->Ranges({
        // scale of the chains
        {1, 4096},
    });
BENCHMARK_REGISTER_F(FilterChainBenchmarkFixture, FilterChainFindTest)
    ->Ranges({
        // scale of the chains
        {1, 4096},
    });

/*
-------------------------------------------------------------------------------------------------------
Benchmark                                                             Time             CPU
Iterations
-------------------------------------------------------------------------------------------------------
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/1         60839 ns        60833 ns 11700
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/8        233775 ns       233759 ns 3137
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/64      2045695 ns      1950081 ns 349
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/512    13860128 ns     13582855 ns 49
FilterChainBenchmarkFixture/FilterChainManagerBuildTest/4096  128333290 ns    127249140 ns 6
FilterChainBenchmarkFixture/FilterChainFindTest/1                   214 ns          214 ns 3230736
FilterChainBenchmarkFixture/FilterChainFindTest/8                  1848 ns         1848 ns 397754
FilterChainBenchmarkFixture/FilterChainFindTest/64                16612 ns        16609 ns 41876
FilterChainBenchmarkFixture/FilterChainFindTest/512              164513 ns       164316 ns 4125
FilterChainBenchmarkFixture/FilterChainFindTest/4096            4066173 ns      4061772 ns 226

*/
} // namespace Server
} // namespace Envoy
BENCHMARK_MAIN();
