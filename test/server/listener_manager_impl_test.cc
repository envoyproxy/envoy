#include "test/server/listener_manager_impl_test.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/listener_manager.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/config/metadata.h"
#include "common/init/manager_impl.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/listener/original_dst/original_dst.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/mocks/init/mocks.h"
#include "test/server/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Server {
namespace {

using testing::AtLeast;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;
using testing::Throw;

class ListenerManagerImplWithDispatcherStatsTest : public ListenerManagerImplTest {
protected:
  ListenerManagerImplWithDispatcherStatsTest() { enable_dispatcher_stats_ = true; }
};

class ListenerManagerImplWithRealFiltersTest : public ListenerManagerImplTest {
public:
  /**
   * Create an IPv4 listener with a given name.
   */
  envoy::config::listener::v3::Listener createIPv4Listener(const std::string& name) {
    envoy::config::listener::v3::Listener listener = parseListenerFromV3Yaml(R"EOF(
      address:
        socket_address: { address: 127.0.0.1, port_value: 1111 }
      filter_chains:
      - filters:
    )EOF");
    listener.set_name(name);
    return listener;
  }

  /**
   * Used by some tests below to validate that, if a given socket option is valid on this platform
   * and set in the Listener, it should result in a call to setsockopt() with the appropriate
   * values.
   */
  void testSocketOption(const envoy::config::listener::v3::Listener& listener,
                        const envoy::config::core::v3::SocketOption::SocketState& expected_state,
                        const Network::SocketOptionName& expected_option, int expected_value,
                        uint32_t expected_num_options = 1,
                        ListenSocketCreationParams expected_creation_params = {true, true}) {
    if (expected_option.hasValue()) {
      expectCreateListenSocket(expected_state, expected_num_options, expected_creation_params);
      expectSetsockopt(expected_option.level(), expected_option.option(), expected_value,
                       expected_num_options);
      manager_->addOrUpdateListener(listener, "", true);
      EXPECT_EQ(1U, manager_->listeners().size());
    } else {
      EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(listener, "", true), EnvoyException,
                                "MockListenerComponentFactory: Setting socket options failed");
      EXPECT_EQ(0U, manager_->listeners().size());
    }
  }
};

class ListenerManagerImplForInPlaceFilterChainUpdateTest : public ListenerManagerImplTest {
public:
  envoy::config::listener::v3::Listener createDefaultListener() {
    envoy::config::listener::v3::Listener listener_proto;
    Protobuf::TextFormat::ParseFromString(R"EOF(
    name: "foo"
    address: {
      socket_address: {
        address: "127.0.0.1"
        port_value: 1234
      }
    }
    filter_chains: {}
  )EOF",
                                          &listener_proto);
    return listener_proto;
  }

  void expectAddListener(const envoy::config::listener::v3::Listener& listener_proto,
                         ListenerHandle*) {
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
    EXPECT_CALL(*worker_, addListener(_, _, _));
    manager_->addOrUpdateListener(listener_proto, "", true);
    worker_->callAddCompletion(true);
    EXPECT_EQ(1UL, manager_->listeners().size());
    checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);
  }

  void expectUpdateToThenDrain(const envoy::config::listener::v3::Listener& new_listener_proto,
                               ListenerHandle* old_listener_handle) {
    EXPECT_CALL(*worker_, addListener(_, _, _));
    EXPECT_CALL(*worker_, stopListener(_, _));
    EXPECT_CALL(*old_listener_handle->drain_manager_, startDrainSequence(_));

    EXPECT_TRUE(manager_->addOrUpdateListener(new_listener_proto, "", true));

    EXPECT_CALL(*worker_, removeListener(_, _));
    old_listener_handle->drain_manager_->drain_sequence_completion_();

    EXPECT_CALL(*old_listener_handle, onDestroy());
    worker_->callRemovalCompletion();
  }

  void expectRemove(const envoy::config::listener::v3::Listener& listener_proto,
                    ListenerHandle* listener_handle) {

    EXPECT_CALL(*worker_, stopListener(_, _));
    EXPECT_CALL(*listener_factory_.socket_, close());
    EXPECT_CALL(*listener_handle->drain_manager_, startDrainSequence(_));
    EXPECT_TRUE(manager_->removeListener(listener_proto.name()));

    EXPECT_CALL(*worker_, removeListener(_, _));
    listener_handle->drain_manager_->drain_sequence_completion_();

    EXPECT_CALL(*listener_handle, onDestroy());
    worker_->callRemovalCompletion();
  }
};

class MockLdsApi : public LdsApi {
public:
  MOCK_METHOD(std::string, versionInfo, (), (const));
};

TEST_F(ListenerManagerImplWithRealFiltersTest, EmptyFilter) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
  EXPECT_EQ(std::chrono::milliseconds(15000),
            manager_->listeners().front().get().listenerFiltersTimeout());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, DefaultListenerPerConnectionBufferLimit) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1024 * 1024U, manager_->listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SetListenerPerConnectionBufferLimit) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
per_connection_buffer_limit_bytes: 8192
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(8192U, manager_->listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsTransportSocket) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  transport_socket:
    name: tls
    typed_config:
      "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
      common_tls_context:
        tls_certificates:
        - certificate_chain:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
          private_key:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
        validation_context:
          trusted_ca:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
          match_subject_alt_names:
            exact: localhost
            exact: 127.0.0.1
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
}

TEST_F(ListenerManagerImplWithRealFiltersTest,
       DEPRECATED_FEATURE_TEST(TlsTransportSocketLegacyConfig)) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  transport_socket:
    name: tls
    typed_config:
      "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
      common_tls_context:
        tls_certificates:
        - certificate_chain:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
          private_key:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
        validation_context:
          trusted_ca:
            filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
          verify_subject_alt_name:
            - localhost
            - 127.0.0.1
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, DEPRECATED_FEATURE_TEST(TlsContext)) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  transport_socket:
     name: tls
     typed_config:
       "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
       common_tls_context:
         tls_certificates:
         - certificate_chain:
             filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
           private_key:
             filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
         validation_context:
           trusted_ca:
             filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
           match_subject_alt_names:
             exact: localhost
             exact: 127.0.0.1
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TransportSocketConnectTimeout) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  transport_socket_connect_timeout: 3s
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_EQ(filter_chain->transportSocketConnectTimeout(), std::chrono::seconds(3));
}

TEST_F(ListenerManagerImplWithRealFiltersTest, UdpAddress) {
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);
  // Validate that there are no active listeners and workers are started.
  EXPECT_EQ(0, server_.stats_store_
                   .gauge("listener_manager.total_active_listeners",
                          Stats::Gauge::ImportMode::NeverImport)
                   .value());
  EXPECT_EQ(1, server_.stats_store_
                   .gauge("listener_manager.workers_started", Stats::Gauge::ImportMode::NeverImport)
                   .value());

  const std::string proto_text = R"EOF(
    address: {
      socket_address: {
        protocol: UDP
        address: "127.0.0.1"
        port_value: 1234
      }
    }
    filter_chains: {}
  )EOF";
  envoy::config::listener::v3::Listener listener_proto;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(proto_text, &listener_proto));

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_CALL(listener_factory_,
              createListenSocket(_, Network::Socket::Type::Datagram, _, {{true, false}}))
      .WillOnce(Invoke([this](const Network::Address::InstanceConstSharedPtr&,
                              Network::Socket::Type, const Network::Socket::OptionsSharedPtr&,
                              const ListenSocketCreationParams&) -> Network::SocketSharedPtr {
        return listener_factory_.socket_;
      }));
  EXPECT_CALL(*listener_factory_.socket_, setSocketOption(_, _, _, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(os_sys_calls_, close(_)).WillRepeatedly(Return(Api::SysCallIntResult{0, errno}));
  manager_->addOrUpdateListener(listener_proto, "", true);
  EXPECT_EQ(1u, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadListenerConfig) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
test: a
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                          EnvoyException, "test: Cannot find field");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadListenerConfigNoFilterChains) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                          EnvoyException, "no filter chains specified");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadListenerConfig2UDPListenerFilters) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    protocol: UDP
    address: 127.0.0.1
    port_value: 1234
listener_filters:
- name: envoy.filters.listener.tls_inspector
- name: envoy.filters.listener.original_dst
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                          EnvoyException, "Only 1 UDP listener filter per listener supported");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadFilterConfig) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  - foo: type
    name: name
    typed_config: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                          EnvoyException, "foo: Cannot find field");
}
class NonTerminalFilterFactory : public Configuration::NamedNetworkFilterConfigFactory {
public:
  // Configuration::NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager&) -> void {};
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return std::make_unique<Envoy::ProtobufWkt::Struct>();
  }

  std::string name() const override { return "non_terminal"; }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, TerminalNotLast) {
  NonTerminalFilterFactory filter;
  Registry::InjectFactory<Configuration::NamedNetworkFilterConfigFactory> registered(filter);

  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  - name: non_terminal
    typed_config: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true), EnvoyException,
      "Error: non-terminal filter named non_terminal of type non_terminal is the last "
      "filter in a network filter chain.");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, NotTerminalLast) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  - name: envoy.filters.network.tcp_proxy
    typed_config: {}
  - name: unknown_but_will_not_be_processed
    typed_config: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true), EnvoyException,
      "Error: terminal filter named envoy.filters.network.tcp_proxy of type "
      "envoy.filters.network.tcp_proxy must be the last filter in a network filter chain.");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadFilterName) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  - name: invalid
    typed_config: {}
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException,
                            "Didn't find a registered implementation for name: 'invalid'");
}

class TestStatsConfigFactory : public Configuration::NamedNetworkFilterConfigFactory {
public:
  // Configuration::NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb createFilterFactoryFromProto(
      const Protobuf::Message&,
      Configuration::FactoryContext& filter_chain_factory_context) override {
    return commonFilterFactory(filter_chain_factory_context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return std::make_unique<Envoy::ProtobufWkt::Struct>();
  }

  std::string name() const override { return "stats_test"; }
  bool isTerminalFilter() override { return true; }

private:
  Network::FilterFactoryCb commonFilterFactory(Configuration::FactoryContext& context) {
    context.scope().counterFromString("bar").inc();
    return [](Network::FilterManager&) -> void {};
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, StatsScopeTest) {
  TestStatsConfigFactory filter;
  Registry::InjectFactory<Configuration::NamedNetworkFilterConfigFactory> registered(filter);

  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
deprecated_v1:
  bind_to_port: false
filter_chains:
- filters:
  - name: stats_test
    typed_config: {}
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, ListenSocketCreationParams(false)));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  manager_->listeners().front().get().listenerScope().counterFromString("foo").inc();

  EXPECT_EQ(1UL, server_.stats_store_.counterFromString("bar").value());
  EXPECT_EQ(1UL, server_.stats_store_.counterFromString("listener.127.0.0.1_1234.foo").value());
}

TEST_F(ListenerManagerImplTest, UnsupportedInternalListener) {
  const std::string yaml = R"EOF(
address:
  envoy_internal_address:
    server_listener_name: a_listener_name  
filter_chains:
- filters: []
  )EOF";

  ASSERT_DEATH(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true), "");
}

TEST_F(ListenerManagerImplTest, NotDefaultListenerFiltersTimeout) {
  const std::string yaml = R"EOF(
    name: "foo"
    address:
      socket_address: { address: 127.0.0.1, port_value: 10000 }
    filter_chains:
    - filters:
    listener_filters_timeout: 0s
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true));
  EXPECT_EQ(std::chrono::milliseconds(),
            manager_->listeners().front().get().listenerFiltersTimeout());
}

TEST_F(ListenerManagerImplTest, ModifyOnlyDrainType) {
  InSequence s;

  // Add foo listener.
  const std::string listener_foo_yaml = R"EOF(
    name: "foo"
    address:
      socket_address: { address: 127.0.0.1, port_value: 10000 }
    filter_chains:
    - filters:
    drain_type: MODIFY_ONLY
  )EOF";

  ListenerHandle* listener_foo =
      expectListenerCreate(false, true, envoy::config::listener::v3::Listener::MODIFY_ONLY);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  EXPECT_CALL(*listener_foo, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddListenerAddressNotMatching) {
  InSequence s;

  // Add foo listener.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
drain_type: default

  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Update foo listener, but with a different address. Should throw.
  const std::string listener_foo_different_address_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1235
filter_chains:
- filters: []
drain_type: modify_only
  )EOF";

  ListenerHandle* listener_foo_different_address =
      expectListenerCreate(false, true, envoy::config::listener::v3::Listener::MODIFY_ONLY);
  EXPECT_CALL(*listener_foo_different_address, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_different_address_yaml),
                                    "", true),
      EnvoyException,
      "error updating listener: 'foo' has a different address "
      "'127.0.0.1:1235' from existing listener address '127.0.0.1:1234'");

  EXPECT_CALL(*listener_foo, onDestroy());
}

// Make sure that a listener creation does not fail on IPv4 only setups when FilterChainMatch is not
// specified and we try to create default CidrRange. See makeCidrListEntry function for
// more details.
TEST_F(ListenerManagerImplTest, AddListenerOnIpv4OnlySetups) {
  InSequence s;

  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
drain_type: default
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, true);

  ON_CALL(os_sys_calls_, socket(AF_INET, _, 0))
      .WillByDefault(Return(Api::SysCallSocketResult{5, 0}));
  ON_CALL(os_sys_calls_, socket(AF_INET6, _, 0))
      .WillByDefault(Return(Api::SysCallSocketResult{INVALID_SOCKET, 0}));
  ON_CALL(os_sys_calls_, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));

  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);
  EXPECT_CALL(*listener_foo, onDestroy());
}

// Make sure that a listener creation does not fail on IPv6 only setups when FilterChainMatch is not
// specified and we try to create default CidrRange. See makeCidrListEntry function for
// more details.
TEST_F(ListenerManagerImplTest, AddListenerOnIpv6OnlySetups) {
  InSequence s;

  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: "::0001"
    port_value: 1234
filter_chains:
- filters: []
drain_type: default
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, true);

  ON_CALL(os_sys_calls_, socket(AF_INET, _, 0))
      .WillByDefault(Return(Api::SysCallSocketResult{INVALID_SOCKET, 0}));
  ON_CALL(os_sys_calls_, socket(AF_INET6, _, 0))
      .WillByDefault(Return(Api::SysCallSocketResult{5, 0}));
  ON_CALL(os_sys_calls_, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));

  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);
  EXPECT_CALL(*listener_foo, onDestroy());
}

// Make sure that a listener that is not added_via_api cannot be updated or removed.
TEST_F(ListenerManagerImplTest, UpdateRemoveNotModifiableListener) {
  time_system_.setSystemTime(std::chrono::milliseconds(1001001001001));

  InSequence s;

  // Add foo listener.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", false));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);
  checkConfigDump(R"EOF(
static_listeners:
  listener:
    "@type": type.googleapis.com/envoy.config.listener.v3.Listener
    name: "foo"
    address:
      socket_address:
        address: "127.0.0.1"
        port_value: 1234
    filter_chains: {}
  last_updated:
    seconds: 1001001001
    nanos: 1000000
)EOF");

  // Update foo listener. Should be blocked.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  - name: fake
    typed_config: {}
  )EOF";

  EXPECT_FALSE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", false));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Remove foo listener. Should be blocked.
  EXPECT_FALSE(manager_->removeListener("foo"));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  EXPECT_CALL(*listener_foo, onDestroy());
}

// Tests that when listener tears down, server's initManager is notified.
TEST_F(ListenerManagerImplTest, ListenerTeardownNotifiesServerInitManager) {
  time_system_.setSystemTime(std::chrono::milliseconds(1001001001001));

  InSequence s;

  auto* lds_api = new MockLdsApi();
  EXPECT_CALL(listener_factory_, createLdsApi_(_, _)).WillOnce(Return(lds_api));
  envoy::config::core::v3::ConfigSource lds_config;
  manager_->createLdsApi(lds_config, nullptr);

  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return(""));
  checkConfigDump(R"EOF(
static_listeners:
)EOF");

  const std::string listener_foo_yaml = R"EOF(
name: "foo"
address:
  socket_address:
    address: "127.0.0.1"
    port_value: 1234
filter_chains: {}
  )EOF";

  const std::string listener_foo_address_update_yaml = R"EOF(
name: "foo"
address:
  socket_address:
    address: "127.0.0.1"
    port_value: 1235
filter_chains: {}
  )EOF";

  Init::ManagerImpl server_init_mgr("server-init-manager");
  Init::ExpectableWatcherImpl server_init_watcher("server-init-watcher");
  { // Add and remove a listener before starting workers.
    ListenerHandle* listener_foo = expectListenerCreate(true, true);
    EXPECT_CALL(server_, initManager()).WillOnce(ReturnRef(server_init_mgr));
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
    EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml),
                                              "version1", true));
    checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

    EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version1"));
    checkConfigDump(R"EOF(
version_info: version1
static_listeners:
dynamic_listeners:
  - name: foo
    warming_state:
      version_info: version1
      listener:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1234
        filter_chains: {}
      last_updated:
        seconds: 1001001001
        nanos: 1000000
)EOF");
    EXPECT_CALL(listener_foo->target_, initialize()).Times(1);
    server_init_mgr.initialize(server_init_watcher);
    // Since listener_foo->target_ is not ready, the listener's listener_init_target will not be
    // ready until the destruction happens.
    server_init_watcher.expectReady().Times(1);
    EXPECT_CALL(*listener_foo, onDestroy());
    EXPECT_TRUE(manager_->removeListener("foo"));
  }
  // Listener foo's listener_init_target_ is the only target added to server_init_mgr.
  EXPECT_EQ(server_init_mgr.state(), Init::Manager::State::Initialized);

  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return(""));
  checkConfigDump(R"EOF(
static_listeners:
)EOF");

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Now add new version listener foo after workers start, note it's fine that server_init_mgr is
  // initialized, as no target will be added to it.
  time_system_.setSystemTime(std::chrono::milliseconds(2002002002002));
  EXPECT_CALL(server_, initManager()).Times(0); // No target added to server init manager.
  server_init_watcher.expectReady().Times(0);
  {
    ListenerHandle* listener_foo2 = expectListenerCreate(true, true);
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
    // Version 2 listener will be initialized by listener manager directly.
    EXPECT_CALL(listener_foo2->target_, initialize()).Times(1);
    EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml),
                                              "version2", true));
    // Version2 is in warming list as listener_foo2->target_ is not ready yet.
    checkStats(__LINE__, /*added=*/2, 0, /*removed=*/1, /*warming=*/1, 0, 0, 0);
    EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version2"));
    checkConfigDump(R"EOF(
  version_info: version2
  static_listeners:
  dynamic_listeners:
    - name: foo
      warming_state:
        version_info: version2
        listener:
          "@type": type.googleapis.com/envoy.config.listener.v3.Listener
          name: foo
          address:
            socket_address:
              address: 127.0.0.1
              port_value: 1234
          filter_chains: {}
        last_updated:
          seconds: 2002002002
          nanos: 2000000
  )EOF");

    // While it is in warming state, try updating the address. It should fail.
    ListenerHandle* listener_foo3 = expectListenerCreate(true, true);
    EXPECT_CALL(*listener_foo3, onDestroy());
    EXPECT_THROW_WITH_MESSAGE(
        manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_address_update_yaml),
                                      "version3", true),
        EnvoyException,
        "error updating listener: 'foo' has a different address "
        "'127.0.0.1:1235' from existing listener address '127.0.0.1:1234'");

    // Delete foo-listener again.
    EXPECT_CALL(*listener_foo2, onDestroy());
    EXPECT_TRUE(manager_->removeListener("foo"));
  }
}

TEST_F(ListenerManagerImplTest, OverrideListener) {
  InSequence s;

  time_system_.setSystemTime(std::chrono::milliseconds(1001001001001));
  auto* lds_api = new MockLdsApi();
  EXPECT_CALL(listener_factory_, createLdsApi_(_, _)).WillOnce(Return(lds_api));
  envoy::config::core::v3::ConfigSource lds_config;
  manager_->createLdsApi(lds_config, nullptr);

  // Add foo listener.
  const std::string listener_foo_yaml = R"EOF(
name: "foo"
address:
  socket_address:
    address: "127.0.0.1"
    port_value: 1234
filter_chains: {}
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "version1", true));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Start workers and capture ListenerImpl.
  Network::ListenerConfig* listener_config = nullptr;
  EXPECT_CALL(*worker_, addListener(_, _, _))
      .WillOnce(Invoke([&listener_config](auto, Network::ListenerConfig& config, auto) -> void {
        listener_config = &config;
      }))
      .RetiresOnSaturation();

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_create_success").value());
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  filter_chain_match:
    destination_port: 1234
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerOverridden(false);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  auto* timer = new Event::MockTimer(dynamic_cast<Event::MockDispatcher*>(&server_.dispatcher()));
  EXPECT_CALL(*timer, enableTimer(_, _));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1UL, manager_->listeners().size());

  worker_->callAddCompletion(true);
  checkStats(__LINE__, 1, 1, 0, 0, 1, 0, 1);

  EXPECT_CALL(*worker_, removeFilterChains(_, _, _));
  timer->invokeCallback();
  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callDrainFilterChainsComplete();

  EXPECT_EQ(1UL, manager_->listeners().size());
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_create_success").value());
}

TEST_F(ListenerManagerImplTest, AddOrUpdateListener) {
  time_system_.setSystemTime(std::chrono::milliseconds(1001001001001));

  InSequence s;

  auto* lds_api = new MockLdsApi();
  EXPECT_CALL(listener_factory_, createLdsApi_(_, _)).WillOnce(Return(lds_api));
  envoy::config::core::v3::ConfigSource lds_config;
  manager_->createLdsApi(lds_config, nullptr);

  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return(""));
  checkConfigDump(R"EOF(
static_listeners:
)EOF");

  // Add foo listener.
  const std::string listener_foo_yaml = R"EOF(
name: "foo"
address:
  socket_address:
    address: "127.0.0.1"
    port_value: 1234
filter_chains: {}
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "version1", true));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);
  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version1"));
  checkConfigDump(R"EOF(
version_info: version1
static_listeners:
dynamic_listeners:
  - name: foo
    warming_state:
      version_info: version1
      listener:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1234
        filter_chains: {}
      last_updated:
        seconds: 1001001001
        nanos: 1000000
)EOF");

  // Update duplicate should be a NOP.
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Update foo listener. Should share socket.
  const std::string listener_foo_update1_yaml = R"EOF(
name: "foo"
address:
  socket_address:
    address: "127.0.0.1"
    port_value: 1234
filter_chains: {}
per_connection_buffer_limit_bytes: 10
  )EOF";

  time_system_.setSystemTime(std::chrono::milliseconds(2002002002002));

  ListenerHandle* listener_foo_update1 = expectListenerCreate(false, true);
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml),
                                            "version2", true));
  checkStats(__LINE__, 1, 1, 0, 0, 1, 0, 0);
  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version2"));
  checkConfigDump(R"EOF(
version_info: version2
static_listeners:
dynamic_listeners:
  - name: foo
    warming_state:
      version_info: version2
      listener:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1234
        filter_chains: {}
        per_connection_buffer_limit_bytes: 10
      last_updated:
        seconds: 2002002002
        nanos: 2000000
)EOF");

  // Validate that workers_started stat is zero before calling startWorkers.
  EXPECT_EQ(0, server_.stats_store_
                   .gauge("listener_manager.workers_started", Stats::Gauge::ImportMode::NeverImport)
                   .value());

  // Start workers.
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);
  // Validate that workers_started stat is still zero before workers set the status via
  // completion callback.
  EXPECT_EQ(0, server_.stats_store_
                   .gauge("listener_manager.workers_started", Stats::Gauge::ImportMode::NeverImport)
                   .value());
  worker_->callAddCompletion(true);

  // Validate that workers_started stat is set to 1 after workers have responded with initialization
  // status.
  EXPECT_EQ(1, server_.stats_store_
                   .gauge("listener_manager.workers_started", Stats::Gauge::ImportMode::NeverImport)
                   .value());

  // Update duplicate should be a NOP.
  EXPECT_FALSE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  checkStats(__LINE__, 1, 1, 0, 0, 1, 0, 0);

  time_system_.setSystemTime(std::chrono::milliseconds(3003003003003));

  // Update foo. Should go into warming, have an immediate warming callback, and start immediate
  // removal.
  ListenerHandle* listener_foo_update2 = expectListenerCreate(false, true);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_foo_update1->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "version3", true));
  worker_->callAddCompletion(true);
  checkStats(__LINE__, 1, 2, 0, 0, 1, 1, 0);
  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version3"));
  checkConfigDump(R"EOF(
version_info: version3
static_listeners:
dynamic_listeners:
  - name: foo
    active_state:
      version_info: version3
      listener:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1234
        filter_chains: {}
      last_updated:
        seconds: 3003003003
        nanos: 3000000
    draining_state:
      version_info: version2
      listener:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1234
        filter_chains: {}
        per_connection_buffer_limit_bytes: 10
      last_updated:
        seconds: 2002002002
        nanos: 2000000
)EOF");

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo_update1->drain_manager_->drain_sequence_completion_();
  checkStats(__LINE__, 1, 2, 0, 0, 1, 1, 0);
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  worker_->callRemovalCompletion();
  checkStats(__LINE__, 1, 2, 0, 0, 1, 0, 0);

  time_system_.setSystemTime(std::chrono::milliseconds(4004004004004));

  // Add bar listener.
  const std::string listener_bar_yaml = R"EOF(
name: "bar"
address:
  socket_address:
    address: "127.0.0.1"
    port_value: 1235
filter_chains: {}
  )EOF";

  ListenerHandle* listener_bar = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_bar_yaml), "version4", true));
  EXPECT_EQ(2UL, manager_->listeners().size());
  worker_->callAddCompletion(true);
  checkStats(__LINE__, 2, 2, 0, 0, 2, 0, 0);

  time_system_.setSystemTime(std::chrono::milliseconds(5005005005005));

  // Add baz listener, this time requiring initializing.
  const std::string listener_baz_yaml = R"EOF(
name: "baz"
address:
  socket_address:
    address: "127.0.0.1"
    port_value: 1236
filter_chains: {}
  )EOF";

  ListenerHandle* listener_baz = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_baz->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_baz_yaml), "version5", true));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(__LINE__, 3, 2, 0, 1, 2, 0, 0);
  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version5"));
  checkConfigDump(R"EOF(
version_info: version5
dynamic_listeners:
  - name: foo
    active_state:
      version_info: version3
      listener:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1234
        filter_chains: {}
      last_updated:
        seconds: 3003003003
        nanos: 3000000
  - name: bar
    active_state:
      version_info: version4
      listener:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: bar
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1235
        filter_chains: {}
      last_updated:
        seconds: 4004004004
        nanos: 4000000
  - name: baz
    warming_state:
      version_info: version5
      listener:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: baz
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1236
        filter_chains: {}
      last_updated:
        seconds: 5005005005
        nanos: 5000000
)EOF");

  // Update a duplicate baz that is currently warming.
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_baz_yaml), "", true));
  checkStats(__LINE__, 3, 2, 0, 1, 2, 0, 0);

  // Update baz while it is warming.
  const std::string listener_baz_update1_yaml = R"EOF(
name: baz
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1236
filter_chains:
- filters:
  - name: fake
    typed_config: {}
  )EOF";

  ListenerHandle* listener_baz_update1 = expectListenerCreate(true, true);
  EXPECT_CALL(*listener_baz, onDestroy()).WillOnce(Invoke([listener_baz]() -> void {
    // Call the initialize callback during destruction like RDS will.
    listener_baz->target_.ready();
  }));
  EXPECT_CALL(listener_baz_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_baz_update1_yaml), "", true));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(__LINE__, 3, 3, 0, 1, 2, 0, 0);

  // Finish initialization for baz which should make it active.
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_baz_update1->target_.ready();
  EXPECT_EQ(3UL, manager_->listeners().size());
  worker_->callAddCompletion(true);
  checkStats(__LINE__, 3, 3, 0, 0, 3, 0, 0);

  EXPECT_CALL(*listener_foo_update2, onDestroy());
  EXPECT_CALL(*listener_bar, onDestroy());
  EXPECT_CALL(*listener_baz_update1, onDestroy());
}

TEST_F(ListenerManagerImplTest, UpdateActiveToWarmAndBack) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add and initialize foo listener.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
per_connection_buffer_limit_bytes: 999
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerCreate(true, true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));

  // Should be both active and warming now.
  EXPECT_EQ(1UL, manager_->listeners(ListenerManager::WARMING).size());
  EXPECT_EQ(1UL, manager_->listeners(ListenerManager::ACTIVE).size());
  checkStats(__LINE__, 1, 1, 0, 1, 1, 0, 0);

  // Update foo back to original active, should cause the warming listener to be removed.
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));

  checkStats(__LINE__, 1, 2, 0, 0, 1, 0, 0);
  EXPECT_EQ(0UL, manager_->listeners(ListenerManager::WARMING).size());
  EXPECT_EQ(1UL, manager_->listeners(ListenerManager::ACTIVE).size());

  EXPECT_CALL(*listener_foo, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddReusableDrainingListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener directly into active.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  Network::Address::InstanceConstSharedPtr local_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 1234));
  ON_CALL(*listener_factory_.socket_, localAddress()).WillByDefault(ReturnRef(local_address));

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  worker_->callAddCompletion(true);
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Remove foo into draining.
  std::function<void()> stop_completion;
  EXPECT_CALL(*worker_, stopListener(_, _))
      .WillOnce(Invoke(
          [&stop_completion](Network::ListenerConfig&, std::function<void()> completion) -> void {
            ASSERT_TRUE(completion != nullptr);
            stop_completion = std::move(completion);
          }));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(__LINE__, 1, 0, 1, 0, 0, 1, 0);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(__LINE__, 1, 0, 1, 0, 0, 1, 0);

  // Add foo again. We should use the socket from draining.
  ListenerHandle* listener_foo2 = expectListenerCreate(false, true);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  worker_->callAddCompletion(true);
  checkStats(__LINE__, 2, 0, 1, 0, 1, 1, 0);

  EXPECT_CALL(*listener_factory_.socket_, close()).Times(0);
  stop_completion();

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  checkStats(__LINE__, 2, 0, 1, 0, 1, 0, 0);

  EXPECT_CALL(*listener_foo2, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddClosedDrainingListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener directly into active.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  Network::Address::InstanceConstSharedPtr local_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 1234));
  ON_CALL(*listener_factory_.socket_, localAddress()).WillByDefault(ReturnRef(local_address));

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  worker_->callAddCompletion(true);
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Remove foo into draining.
  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_factory_.socket_, close());
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(__LINE__, 1, 0, 1, 0, 0, 1, 0);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(__LINE__, 1, 0, 1, 0, 0, 1, 0);

  // Add foo again. We should use the socket from draining.
  ListenerHandle* listener_foo2 = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  worker_->callAddCompletion(true);
  checkStats(__LINE__, 2, 0, 1, 0, 1, 1, 0);

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  checkStats(__LINE__, 2, 0, 1, 0, 1, 0, 0);

  EXPECT_CALL(*listener_foo2, onDestroy());
}

TEST_F(ListenerManagerImplTest, BindToPortEqualToFalse) {
  InSequence s;
  ProdListenerComponentFactory real_listener_factory(server_);
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
deprecated_v1:
    bind_to_port: false
filter_chains:
- filters: []
  )EOF";

  auto syscall_result = os_sys_calls_actual_.socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(SOCKET_VALID(syscall_result.rc_));

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, ListenSocketCreationParams(false)))
      .WillOnce(Invoke([this, &syscall_result, &real_listener_factory](
                           const Network::Address::InstanceConstSharedPtr& address,
                           Network::Socket::Type socket_type,
                           const Network::Socket::OptionsSharedPtr& options,
                           const ListenSocketCreationParams& params) -> Network::SocketSharedPtr {
        EXPECT_CALL(server_, hotRestart).Times(0);
        // When bind_to_port is equal to false, create socket fd directly, and do not get socket
        // fd through hot restart.
        ON_CALL(os_sys_calls_, socket(AF_INET, _, 0)).WillByDefault(Return(syscall_result));
        return real_listener_factory.createListenSocket(address, socket_type, options, params);
      }));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
}

TEST_F(ListenerManagerImplTest, ReusePortEqualToTrue) {
  InSequence s;
  ProdListenerComponentFactory real_listener_factory(server_);
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 0
reuse_port: true
filter_chains:
- filters: []
  )EOF";

  auto syscall_result = os_sys_calls_actual_.socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(SOCKET_VALID(syscall_result.rc_));

  // On Windows if the socket has not been bound to an address with bind
  // the call to getsockname fails with WSAEINVAL. To avoid that we make sure
  // that the bind system actually happens and it does not get mocked.
  ON_CALL(os_sys_calls_, bind(_, _, _))
      .WillByDefault(Invoke(
          [&](os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) -> Api::SysCallIntResult {
            Api::SysCallIntResult result = os_sys_calls_actual_.bind(sockfd, addr, addrlen);
            ASSERT(result.rc_ >= 0);
            return result;
          }));
  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {{true, false}}))
      .WillOnce(Invoke([this, &syscall_result, &real_listener_factory](
                           const Network::Address::InstanceConstSharedPtr& address,
                           Network::Socket::Type socket_type,
                           const Network::Socket::OptionsSharedPtr& options,
                           const ListenSocketCreationParams& params) -> Network::SocketSharedPtr {
        EXPECT_CALL(server_, hotRestart).Times(0);
        ON_CALL(os_sys_calls_, socket(AF_INET, _, 0)).WillByDefault(Return(syscall_result));
        return real_listener_factory.createListenSocket(address, socket_type, options, params);
      }));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
}

TEST_F(ListenerManagerImplTest, NotSupportedDatagramUds) {
  ProdListenerComponentFactory real_listener_factory(server_);
  EXPECT_THROW_WITH_MESSAGE(real_listener_factory.createListenSocket(
                                std::make_shared<Network::Address::PipeInstance>("/foo"),
                                Network::Socket::Type::Datagram, nullptr, {true}),
                            EnvoyException,
                            "socket type SocketType::Datagram not supported for pipes");
}

TEST_F(ListenerManagerImplTest, CantBindSocket) {
  time_system_.setSystemTime(std::chrono::milliseconds(1001001001001));
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}))
      .WillOnce(Throw(EnvoyException("can't bind")));
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_THROW(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true),
               EnvoyException);
  checkConfigDump(R"EOF(
dynamic_listeners:
  - name: foo
    error_state:
      failed_configuration:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 1234
        filter_chains:
          - {}
      last_update_attempt:
        seconds: 1001001001
        nanos: 1000000
      details: can't bind
)EOF");

  ListenerManager::FailureStates empty_failure_state;
  // Fake a new update, just to sanity check the clearing code.
  manager_->beginListenerUpdate();
  manager_->endListenerUpdate(std::move(empty_failure_state));

  checkConfigDump(R"EOF(
dynamic_listeners:
)EOF");
}

// Verify that errors tracked on endListenerUpdate show up in the config dump/
TEST_F(ListenerManagerImplTest, ConfigDumpWithExternalError) {
  time_system_.setSystemTime(std::chrono::milliseconds(1001001001001));
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Make sure the config dump is empty by default.
  ListenerManager::FailureStates empty_failure_state;
  manager_->beginListenerUpdate();
  manager_->endListenerUpdate(std::move(empty_failure_state));
  checkConfigDump(R"EOF(
dynamic_listeners:
)EOF");

  // Now have an external update with errors and make sure it gets dumped.
  ListenerManager::FailureStates non_empty_failure_state;
  non_empty_failure_state.push_back(std::make_unique<envoy::admin::v3::UpdateFailureState>());
  auto& state = non_empty_failure_state.back();
  state->set_details("foo");
  manager_->beginListenerUpdate();
  manager_->endListenerUpdate(std::move(non_empty_failure_state));
  checkConfigDump(R"EOF(
dynamic_listeners:
  error_state:
    details: "foo"
)EOF");

  // And clear again.
  ListenerManager::FailureStates empty_failure_state2;
  manager_->beginListenerUpdate();
  manager_->endListenerUpdate(std::move(empty_failure_state2));
  checkConfigDump(R"EOF(
dynamic_listeners:
)EOF");
}

TEST_F(ListenerManagerImplTest, ListenerDraining) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  worker_->callAddCompletion(true);
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(server_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_FALSE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(__LINE__, 1, 0, 1, 0, 0, 1, 0);

  // NOTE: || short circuit here prevents the server drain manager from getting called.
  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(true));
  EXPECT_TRUE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(__LINE__, 1, 0, 1, 0, 0, 1, 0);

  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(server_.drain_manager_, drainClose()).WillOnce(Return(true));
  EXPECT_TRUE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 1, 0, 0, 0, 0);
}

TEST_F(ListenerManagerImplTest, RemoveListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Remove an unknown listener.
  EXPECT_FALSE(manager_->removeListener("unknown"));

  // Add foo listener into warming.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);

  // Remove foo.
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->removeListener("foo"));
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 1, 0, 0, 0, 0);

  // Add foo again and initialize it.
  listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 2, 0, 1, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 2, 0, 1, 0, 1, 0, 0);

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
per_connection_buffer_limit_bytes: 999
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerCreate(true, true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 2, 1, 1, 1, 1, 0, 0);

  // Remove foo which should remove both warming and active.
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_factory_.socket_, close());
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(__LINE__, 2, 1, 2, 0, 0, 1, 0);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(__LINE__, 2, 1, 2, 0, 0, 1, 0);
  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(__LINE__, 2, 1, 2, 0, 0, 0, 0);
}

// Validates that StopListener functionality works correctly when only inbound listeners are
// stopped.
TEST_F(ListenerManagerImplTest, StopListeners) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener in inbound direction.
  const std::string listener_foo_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  auto foo_inbound_proto = parseListenerFromV3Yaml(listener_foo_yaml);
  EXPECT_TRUE(manager_->addOrUpdateListener(foo_inbound_proto, "", true));
  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Add a listener in outbound direction.
  const std::string listener_foo_outbound_yaml = R"EOF(
name: foo_outbound
traffic_direction: OUTBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1239
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo_outbound = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo_outbound->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_outbound_yaml), "", true));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo_outbound->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(2UL, manager_->listeners().size());

  // Validate that stop listener is only called once - for inbound listeners.
  EXPECT_CALL(*worker_, stopListener(_, _)).Times(1);
  EXPECT_CALL(*listener_factory_.socket_, close()).Times(1);
  manager_->stopListeners(ListenerManager::StopListenersType::InboundOnly);
  EXPECT_EQ(1, server_.stats_store_.counterFromString("listener_manager.listener_stopped").value());

  // Validate that listener creation in outbound direction is allowed.
  const std::string listener_bar_outbound_yaml = R"EOF(
name: bar_outbound
traffic_direction: OUTBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1237
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_bar_outbound = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_bar_outbound_yaml), "", true));
  EXPECT_EQ(3UL, manager_->listeners().size());
  worker_->callAddCompletion(true);

  // Validate that adding a listener in stopped listener's traffic direction is not allowed.
  const std::string listener_bar_yaml = R"EOF(
name: bar
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1235
filter_chains:
- filters: []
  )EOF";
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_bar_yaml), "", true));

  // Explicitly validate that in place filter chain update is not allowed.
  auto in_place_foo_inbound_proto = foo_inbound_proto;
  in_place_foo_inbound_proto.mutable_filter_chains(0)
      ->mutable_filter_chain_match()
      ->mutable_destination_port()
      ->set_value(9999);

  EXPECT_FALSE(manager_->addOrUpdateListener(in_place_foo_inbound_proto, "", true));
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_CALL(*listener_foo_outbound, onDestroy());
  EXPECT_CALL(*listener_bar_outbound, onDestroy());
}

// Validates that StopListener functionality works correctly when all listeners are stopped.
TEST_F(ListenerManagerImplTest, StopAllListeners) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into warming.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_factory_.socket_, close());
  EXPECT_CALL(*listener_foo, onDestroy());
  manager_->stopListeners(ListenerManager::StopListenersType::All);
  EXPECT_EQ(1, server_.stats_store_.counterFromString("listener_manager.listener_stopped").value());

  // Validate that adding a listener is not allowed after all listeners are stopped.
  const std::string listener_bar_yaml = R"EOF(
name: bar
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1235
filter_chains:
- filters: []
  )EOF";
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_bar_yaml), "", true));
}

// Validate that stopping a warming listener, removes directly from warming listener list.
TEST_F(ListenerManagerImplTest, StopWarmingListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into warming.
  const std::string listener_foo_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
per_connection_buffer_limit_bytes: 999
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerCreate(true, true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1UL, manager_->listeners().size());

  // Stop foo which should remove warming listener.
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_factory_.socket_, close());
  EXPECT_CALL(*listener_foo, onDestroy());
  manager_->stopListeners(ListenerManager::StopListenersType::InboundOnly);
  EXPECT_EQ(1, server_.stats_store_.counterFromString("listener_manager.listener_stopped").value());
}

TEST_F(ListenerManagerImplTest, AddListenerFailure) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into active.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 0.0.0.0
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));

  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  worker_->callAddCompletion(false);

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();

  EXPECT_EQ(
      1UL,
      server_.stats_store_.counterFromString("listener_manager.listener_create_failure").value());
}

TEST_F(ListenerManagerImplTest, StaticListenerAddFailure) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into active.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 0.0.0.0
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", false));

  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  worker_->callAddCompletion(false);

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();

  EXPECT_EQ(
      1UL,
      server_.stats_store_.counterFromString("listener_manager.listener_create_failure").value());
  EXPECT_EQ(0, manager_->listeners().size());
}

TEST_F(ListenerManagerImplTest, StatsNameValidCharacterTest) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: "::1"
    port_value: 10000
filter_chains:
- filters: []
  )EOF";

  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  manager_->listeners().front().get().listenerScope().counterFromString("foo").inc();

  EXPECT_EQ(1UL, server_.stats_store_.counterFromString("listener.[__1]_10000.foo").value());
}

TEST_F(ListenerManagerImplTest, DuplicateAddressDontBind) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into warming.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 0.0.0.0
    port_value: 1234
deprecated_v1:
  bind_to_port: false
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, ListenSocketCreationParams(false)));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));

  // Add bar with same non-binding address. Should fail.
  const std::string listener_bar_yaml = R"EOF(
name: bar
address:
  socket_address:
    address: 0.0.0.0
    port_value: 1234
deprecated_v1:
  bind_to_port: false
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_bar = expectListenerCreate(true, true);
  EXPECT_CALL(*listener_bar, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_bar_yaml), "", true),
      EnvoyException,
      "error adding listener: 'bar' has duplicate address '0.0.0.0:1234' as existing listener");

  // Move foo to active and then try to add again. This should still fail.
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);

  listener_bar = expectListenerCreate(true, true);
  EXPECT_CALL(*listener_bar, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_bar_yaml), "", true),
      EnvoyException,
      "error adding listener: 'bar' has duplicate address '0.0.0.0:1234' as existing listener");

  EXPECT_CALL(*listener_foo, onDestroy());
}

TEST_F(ListenerManagerImplTest, EarlyShutdown) {
  // If stopWorkers is called before the workers are started, it should be a no-op: they should be
  // neither started nor stopped.
  EXPECT_CALL(*worker_, start(_)).Times(0);
  EXPECT_CALL(*worker_, stop()).Times(0);
  manager_->stopWorkers();
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithDestinationPortMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        destination_port: 8080
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // IPv4 client connects to unknown port - no match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // IPv4 client connects to valid port - using 1st filter chain.
  filter_chain = findFilterChain(8080, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // UDS client - no match.
  filter_chain = findFilterChain(0, "/tmp/test.sock", "", "tls", {}, "/tmp/test.sock", 111);
  EXPECT_EQ(filter_chain, nullptr);
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithDestinationIPMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        prefix_ranges: { address_prefix: 127.0.0.0, prefix_len: 8 }
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // IPv4 client connects to unknown IP - no match.
  auto filter_chain = findFilterChain(1234, "1.2.3.4", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // IPv4 client connects to valid IP - using 1st filter chain.
  filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // UDS client - no match.
  filter_chain = findFilterChain(0, "/tmp/test.sock", "", "tls", {}, "/tmp/test.sock", 111);
  EXPECT_EQ(filter_chain, nullptr);
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithServerNamesMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "server1.example.com"
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS client without SNI - no match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // TLS client without matching SNI - no match.
  filter_chain = findFilterChain(1234, "127.0.0.1", "www.example.com", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // TLS client with matching SNI - using 1st filter chain.
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "server1.example.com", "tls", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithTransportProtocolMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        transport_protocol: "tls"
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TCP client - no match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "raw_buffer", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // TLS client - using 1st filter chain.
  filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithApplicationProtocolMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        application_protocols: "http/1.1"
        source_type: ANY
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS client without ALPN - no match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // TLS client with "http/1.1" ALPN - using 1st filter chain.
  filter_chain = findFilterChain(
      1234, "127.0.0.1", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "8.8.8.8",
      111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");
}

// Define a source_type filter chain match and test against it.
TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithSourceTypeMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        source_type: SAME_IP_OR_LOOPBACK
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // EXTERNAL IPv4 client without "http/1.1" ALPN - no match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // LOCAL IPv4 client with "http/1.1" ALPN - using 1st filter chain.
  filter_chain = findFilterChain(
      1234, "127.0.0.1", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "127.0.0.1",
      111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // LOCAL UDS client with "http/1.1" ALPN - using 1st filter chain.
  filter_chain = findFilterChain(
      0, "/tmp/test.sock", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11},
      "/tmp/test.sock", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");
}

// Verify source IP matches.
TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithSourceIpMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        source_prefix_ranges:
          - address_prefix: 10.0.0.1
            prefix_len: 24
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // IPv4 client with source 10.0.1.1. No match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "10.0.1.1", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // IPv4 client with source 10.0.0.10, Match.
  filter_chain = findFilterChain(
      1234, "127.0.0.1", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "10.0.0.10",
      111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // IPv6 client. No match.
  filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {},
                                 "2001:0db8:85a3:0000:0000:8a2e:0370:7334", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // UDS client. No match.
  filter_chain = findFilterChain(
      0, "/tmp/test.sock", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11},
      "/tmp/test.sock", 0);
  ASSERT_EQ(filter_chain, nullptr);
}

// Verify source IPv6 matches.
TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithSourceIpv6Match) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        source_prefix_ranges:
          - address_prefix: 2001:0db8:85a3:0000:0000:0000:0000:0000
            prefix_len: 64
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // IPv6 client with matching subnet. Match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {},
                                      "2001:0db8:85a3:0000:0000:8a2e:0370:7334", 111);
  EXPECT_NE(filter_chain, nullptr);

  // IPv6 client with non-matching subnet. No match.
  filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {},
                                 "2001:0db8:85a3:0001:0000:8a2e:0370:7334", 111);
  EXPECT_EQ(filter_chain, nullptr);
}

// Verify source port matches.
TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithSourcePortMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        source_ports:
          - 100
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // Client with source port 100. Match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 100);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // Client with source port 101. No match.
  filter_chain = findFilterChain(
      1234, "8.8.8.8", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "4.4.4.4",
      101);
  ASSERT_EQ(filter_chain, nullptr);
}

// Define multiple source_type filter chain matches and test against them.
TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainWithSourceTypeMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        source_type: SAME_IP_OR_LOOPBACK
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
    - filter_chain_match:
        application_protocols: "http/1.1"
        source_type: EXTERNAL
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem" }
    - filter_chain_match:
        source_type: ANY
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // LOCAL TLS client with "http/1.1" ALPN - no match.
  auto filter_chain = findFilterChain(
      1234, "127.0.0.1", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "127.0.0.1",
      111);
  EXPECT_EQ(filter_chain, nullptr);

  // LOCAL TLS client without "http/1.1" ALPN - using 1st filter chain.
  filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // EXTERNAL TLS client with "http/1.1" ALPN - using 2nd filter chain.
  filter_chain = findFilterChain(
      1234, "8.8.8.8", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "4.4.4.4",
      111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto uri = ssl_socket->ssl()->uriSanLocalCertificate();
  EXPECT_EQ(uri[0], "spiffe://lyft.com/test-team");

  // EXTERNAL TLS client without "http/1.1" ALPN - using 3rd filter chain.
  filter_chain = findFilterChain(1234, "8.8.8.8", "", "tls", {}, "4.4.4.4", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 2);
  EXPECT_EQ(server_names.front(), "*.example.com");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithDestinationPortMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        # empty
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem" }
    - filter_chain_match:
        destination_port: 8080
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
    - filter_chain_match:
        destination_port: 8081
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // IPv4 client connects to default port - using 1st filter chain.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto uri = ssl_socket->ssl()->uriSanLocalCertificate();
  EXPECT_EQ(uri[0], "spiffe://lyft.com/test-team");

  // IPv4 client connects to port 8080 - using 2nd filter chain.
  filter_chain = findFilterChain(8080, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // IPv4 client connects to port 8081 - using 3rd filter chain.
  filter_chain = findFilterChain(8081, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 2);
  EXPECT_EQ(server_names.front(), "*.example.com");

  // UDS client - using 1st filter chain.
  filter_chain = findFilterChain(0, "/tmp/test.sock", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  uri = ssl_socket->ssl()->uriSanLocalCertificate();
  EXPECT_EQ(uri[0], "spiffe://lyft.com/test-team");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithDestinationIPMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        # empty
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem" }
    - filter_chain_match:
        prefix_ranges: { address_prefix: 192.168.0.1, prefix_len: 32 }
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
    - filter_chain_match:
        prefix_ranges: { address_prefix: 192.168.0.0, prefix_len: 16 }
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // IPv4 client connects to default IP - using 1st filter chain.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto uri = ssl_socket->ssl()->uriSanLocalCertificate();
  EXPECT_EQ(uri[0], "spiffe://lyft.com/test-team");

  // IPv4 client connects to exact IP match - using 2nd filter chain.
  filter_chain = findFilterChain(1234, "192.168.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // IPv4 client connects to wildcard IP match - using 3rd filter chain.
  filter_chain = findFilterChain(1234, "192.168.1.1", "", "tls", {}, "192.168.1.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 2);
  EXPECT_EQ(server_names.front(), "*.example.com");

  // UDS client - using 1st filter chain.
  filter_chain = findFilterChain(0, "/tmp/test.sock", "", "tls", {}, "/tmp/test.sock", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  uri = ssl_socket->ssl()->uriSanLocalCertificate();
  EXPECT_EQ(uri[0], "spiffe://lyft.com/test-team");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithServerNamesMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        # empty
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filter_chain_match:
        server_names: "server1.example.com"
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filter_chain_match:
        server_names: "*.com"
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
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS client without SNI - using 1st filter chain.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto uri = ssl_socket->ssl()->uriSanLocalCertificate();
  EXPECT_EQ(uri[0], "spiffe://lyft.com/test-team");

  // TLS client with exact SNI match - using 2nd filter chain.
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "server1.example.com", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // TLS client with wildcard SNI match - using 3rd filter chain.
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "server2.example.com", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 2);
  EXPECT_EQ(server_names.front(), "*.example.com");

  // TLS client with wildcard SNI match - using 3rd filter chain.
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "www.wildcard.com", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 2);
  EXPECT_EQ(server_names.front(), "*.example.com");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithTransportProtocolMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        # empty
    - filter_chain_match:
        transport_protocol: "tls"
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TCP client - using 1st filter chain.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "raw_buffer", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_FALSE(filter_chain->transportSocketFactory().implementsSecureTransport());

  // TLS client - using 2nd filter chain.
  filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithApplicationProtocolMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        # empty
    - filter_chain_match:
        application_protocols: ["dummy", "h2"]
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS client without ALPN - using 1st filter chain.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_FALSE(filter_chain->transportSocketFactory().implementsSecureTransport());

  // TLS client with "h2,http/1.1" ALPN - using 2nd filter chain.
  filter_chain = findFilterChain(
      1234, "127.0.0.1", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "127.0.0.1",
      111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithMultipleRequirementsMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        # empty
    - filter_chain_match:
        server_names: ["www.example.com", "server1.example.com"]
        transport_protocol: "tls"
        application_protocols: ["dummy", "h2"]
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS client without SNI and ALPN - using 1st filter chain.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_FALSE(filter_chain->transportSocketFactory().implementsSecureTransport());

  // TLS client with exact SNI match but without ALPN - no match (SNI blackholed by configuration).
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "server1.example.com", "tls", {}, "127.0.0.1", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // TLS client with ALPN match but without SNI - using 1st filter chain.
  filter_chain = findFilterChain(
      1234, "127.0.0.1", "", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "127.0.0.1",
      111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_FALSE(filter_chain->transportSocketFactory().implementsSecureTransport());

  // TLS client with exact SNI match and ALPN match - using 2nd filter chain.
  filter_chain = findFilterChain(
      1234, "127.0.0.1", "server1.example.com", "tls",
      {Http::Utility::AlpnNames::get().Http2, Http::Utility::AlpnNames::get().Http11}, "127.0.0.1",
      111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithDifferentSessionTicketKeys) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "example.com"
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filter_chain_match:
        server_names: "www.example.com"
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_b"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest,
       MultipleFilterChainsWithMixedUseOfSessionTicketKeys) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "example.com"
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filter_chain_match:
        server_names: "www.example.com"
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithInvalidDestinationIPMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        prefix_ranges: { address_prefix: a.b.c.d, prefix_len: 32 }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException, "malformed IP address: a.b.c.d");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithInvalidServerNamesMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "*w.example.com"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': partial wildcards are not "
                            "supported in \"server_names\"");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithSameMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - name : foo
      filter_chain_match:
        transport_protocol: "tls"
    - name: bar
      filter_chain_match:
        transport_protocol: "tls"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': filter chain 'bar' has "
                            "the same matching rules defined as 'foo'");
}

TEST_F(ListenerManagerImplWithRealFiltersTest,
       MultipleFilterChainsWithSameMatchPlusUnimplementedFields) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - name: foo
      filter_chain_match:
        transport_protocol: "tls"
    - name: bar
      filter_chain_match:
        transport_protocol: "tls"
        address_suffix: 127.0.0.0
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true), EnvoyException,
      "error adding listener '127.0.0.1:1234': filter chain 'bar' contains unimplemented fields");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithOverlappingRules) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "example.com"
    - filter_chain_match:
        server_names: ["example.com", "www.example.com"]
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': multiple filter chains with "
                            "overlapping matching rules are defined");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsFilterChainWithoutTlsInspector) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        transport_protocol: "tls"
    - filter_chain_match:
        # empty
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS Inspector is automatically injected for filter chains with TLS requirements,
  // so make sure there is exactly 1 listener filter (and assume it's TLS Inspector).
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&](const Network::ListenerFilterMatcherSharedPtr&,
                           Network::ListenerFilterPtr&) -> void {}));
  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
}

// Test the tls inspector is not injected twice when the deprecated name is used.
TEST_F(ListenerManagerImplWithRealFiltersTest,
       DEPRECATED_FEATURE_TEST(TlsFilterChainWithDeprecatedTlsInspectorName)) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      typed_config: {}
    filter_chains:
    - filter_chain_match:
        transport_protocol: "tls"
    - filter_chain_match:
        # empty
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // Make sure there is exactly 1 listener filter (and assume it's TLS Inspector). 2 filters
  // would imply incorrect injection of a second filter.
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&](const Network::ListenerFilterMatcherSharedPtr&,
                           Network::ListenerFilterPtr&) -> void {}));
  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SniFilterChainWithoutTlsInspector) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        server_names: "example.com"
    - filter_chain_match:
        # empty
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS Inspector is automatically injected for filter chains with SNI requirements,
  // so make sure there is exactly 1 listener filter (and assume it's TLS Inspector).
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&](const Network::ListenerFilterMatcherSharedPtr&,
                           Network::ListenerFilterPtr&) -> void {}));
  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
}

TEST_F(ListenerManagerImplWithRealFiltersTest, AlpnFilterChainWithoutTlsInspector) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        application_protocols: ["h2", "http/1.1"]
    - filter_chain_match:
        # empty
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS Inspector is automatically injected for filter chains with ALPN requirements,
  // so make sure there is exactly 1 listener filter (and assume it's TLS Inspector).
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&](const Network::ListenerFilterMatcherSharedPtr&,
                           Network::ListenerFilterPtr&) -> void {}));
  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
}

TEST_F(ListenerManagerImplWithRealFiltersTest, CustomTransportProtocolWithSniWithoutTlsInspector) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        server_names: "example.com"
        transport_protocol: "custom"
    - filter_chain_match:
        # empty
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // Make sure there are no listener filters (i.e. no automatically injected TLS Inspector).
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_, _)).Times(0);
  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateInline) {
  const std::string cert = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem"));
  const std::string pkey = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem"));
  const std::string ca = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"));
  const std::string yaml = absl::StrCat(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { inline_string: ")EOF",
                                        absl::CEscape(cert), R"EOF(" }
                private_key: { inline_string: ")EOF",
                                        absl::CEscape(pkey), R"EOF(" }
            validation_context:
              trusted_ca: { inline_string: ")EOF",
                                        absl::CEscape(ca), R"EOF(" }
  )EOF");

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateChainInlinePrivateKeyFilename) {
  const std::string cert = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem"));
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem" }
                certificate_chain: { inline_string: ")EOF",
                                                                    absl::CEscape(cert), R"EOF(" }
  )EOF"),
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateIncomplete) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true), EnvoyException,
      TestEnvironment::substitute(
          "Failed to load incomplete certificate from {{ test_rundir }}"
          "/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem, ",
          Network::Address::IpVersion::v4));
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateInvalidCertificateChain) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { inline_string: "invalid" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException, "Failed to load certificate chain from <inline>");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateInvalidIntermediateCA) {
  const std::string leaf = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_cert.pem"));
  const std::string yaml = TestEnvironment::substitute(
      absl::StrCat(
          R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { inline_string: ")EOF",
          absl::CEscape(leaf),
          R"EOF(\n-----BEGIN CERTIFICATE-----\nDEFINITELY_INVALID_CERTIFICATE\n-----END CERTIFICATE-----" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem" }
  )EOF"),
      Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException, "Failed to load certificate chain from <inline>");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateInvalidPrivateKey) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem" }
                private_key: { inline_string: "invalid" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException, "Failed to load private key from <inline>");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateInvalidTrustedCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem" }
            validation_context:
              trusted_ca: { inline_string: "invalid" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException, "Failed to load trusted CA certificates from <inline>");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, Metadata) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    metadata: { filter_metadata: { com.bar.foo: { baz: test_value } } }
    traffic_direction: INBOUND
    filter_chains:
    - filter_chain_match:
      filters:
      - name: http
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: metadata_test
          route_config:
            virtual_hosts:
            - name: "some_virtual_host"
              domains: ["some.domain"]
              routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo }
    listener_filters:
    - name: "envoy.filters.listener.original_dst"
      typed_config: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);
  Configuration::ListenerFactoryContext* listener_factory_context = nullptr;
  // Extract listener_factory_context avoid accessing private member.
  ON_CALL(listener_factory_, createListenerFilterFactoryList(_, _))
      .WillByDefault(
          Invoke([&listener_factory_context](
                     const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                         filters,
                     Configuration::ListenerFactoryContext& context)
                     -> std::vector<Network::ListenerFilterFactoryCb> {
            listener_factory_context = &context;
            return ProdListenerComponentFactory::createListenerFilterFactoryList_(filters, context);
          }));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  ASSERT_NE(nullptr, listener_factory_context);
  EXPECT_EQ("test_value", Config::Metadata::metadataValue(
                              &listener_factory_context->listenerMetadata(), "com.bar.foo", "baz")
                              .string_value());
  EXPECT_EQ(envoy::config::core::v3::INBOUND, listener_factory_context->direction());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, OriginalDstFilter) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1111 }
    filter_chains: {}
    listener_filters:
    - name: "envoy.filters.listener.original_dst"
      typed_config: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  Network::ListenerConfig& listener = manager_->listeners().back().get();

  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;

#ifdef SOL_IP
  // Return error when trying to retrieve the original dst on the invalid handle
  EXPECT_CALL(os_sys_calls_, getsockopt_(_, _, _, _, _)).WillOnce(Return(-1));
#endif

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  Network::AcceptedSocketImpl socket(std::make_unique<Network::IoSocketHandleImpl>(),
                                     Network::Address::InstanceConstSharedPtr{
                                         new Network::Address::Ipv4Instance("127.0.0.1", 1234)},
                                     Network::Address::InstanceConstSharedPtr{
                                         new Network::Address::Ipv4Instance("127.0.0.1", 5678)});

  EXPECT_CALL(callbacks, socket()).WillOnce(Invoke([&]() -> Network::ConnectionSocket& {
    return socket;
  }));

  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&](const Network::ListenerFilterMatcherSharedPtr&,
                           Network::ListenerFilterPtr& filter) -> void {
        EXPECT_EQ(Network::FilterStatus::Continue, filter->onAccept(callbacks));
      }));

  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
}

class OriginalDstTestFilter : public Extensions::ListenerFilters::OriginalDst::OriginalDstFilter {
  Network::Address::InstanceConstSharedPtr getOriginalDst(Network::Socket&) override {
    return Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("127.0.0.2", 2345)};
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, OriginalDstTestFilter) {
  class OriginalDstTestConfigFactory : public Configuration::NamedListenerFilterConfigFactory {
  public:
    // NamedListenerFilterConfigFactory
    Network::ListenerFilterFactoryCb
    createListenerFilterFactoryFromProto(const Protobuf::Message&,
                                         const Network::ListenerFilterMatcherSharedPtr&,
                                         Configuration::ListenerFactoryContext&) override {
      return [](Network::ListenerFilterManager& filter_manager) -> void {
        filter_manager.addAcceptFilter(nullptr, std::make_unique<OriginalDstTestFilter>());
      };
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      // Using Struct instead of a custom per-filter empty config proto
      // This is only allowed in tests.
      return std::make_unique<Envoy::ProtobufWkt::Struct>();
    }

    std::string name() const override { return "test.listener.original_dst"; }
  };

  OriginalDstTestConfigFactory factory;
  Registry::InjectFactory<Configuration::NamedListenerFilterConfigFactory> registration(factory);

  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1111 }
    filter_chains: {}
    listener_filters:
    - name: "test.listener.original_dst"
      typed_config: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  Network::ListenerConfig& listener = manager_->listeners().back().get();

  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  Network::AcceptedSocketImpl socket(
      std::make_unique<Network::IoSocketHandleImpl>(),
      std::make_unique<Network::Address::Ipv4Instance>("127.0.0.1", 1234),
      std::make_unique<Network::Address::Ipv4Instance>("127.0.0.1", 5678));

  EXPECT_CALL(callbacks, socket()).WillOnce(Invoke([&]() -> Network::ConnectionSocket& {
    return socket;
  }));

  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&](const Network::ListenerFilterMatcherSharedPtr&,
                           Network::ListenerFilterPtr& filter) -> void {
        EXPECT_EQ(Network::FilterStatus::Continue, filter->onAccept(callbacks));
      }));

  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
  EXPECT_TRUE(socket.localAddressRestored());
  EXPECT_EQ("127.0.0.2:2345", socket.localAddress()->asString());
}

class OriginalDstTestFilterIPv6
    : public Extensions::ListenerFilters::OriginalDst::OriginalDstFilter {
  Network::Address::InstanceConstSharedPtr getOriginalDst(Network::Socket&) override {
    return Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance("1::2", 2345)};
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, OriginalDstTestFilterIPv6) {
  class OriginalDstTestConfigFactory : public Configuration::NamedListenerFilterConfigFactory {
  public:
    // NamedListenerFilterConfigFactory
    Network::ListenerFilterFactoryCb
    createListenerFilterFactoryFromProto(const Protobuf::Message&,
                                         const Network::ListenerFilterMatcherSharedPtr&,
                                         Configuration::ListenerFactoryContext&) override {
      return [](Network::ListenerFilterManager& filter_manager) -> void {
        filter_manager.addAcceptFilter(nullptr, std::make_unique<OriginalDstTestFilterIPv6>());
      };
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      // Using Struct instead of a custom per-filter empty config proto
      // This is only allowed in tests.
      return std::make_unique<Envoy::ProtobufWkt::Struct>();
    }

    std::string name() const override { return "test.listener.original_dstipv6"; }
  };

  OriginalDstTestConfigFactory factory;
  Registry::InjectFactory<Configuration::NamedListenerFilterConfigFactory> registration(factory);

  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: ::0001, port_value: 1111 }
    filter_chains: {}
    listener_filters:
    - name: "test.listener.original_dstipv6"
      typed_config: {}
  )EOF",
                                                       Network::Address::IpVersion::v6);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  Network::ListenerConfig& listener = manager_->listeners().back().get();

  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  Network::AcceptedSocketImpl socket(
      std::make_unique<Network::IoSocketHandleImpl>(),
      std::make_unique<Network::Address::Ipv6Instance>("::0001", 1234),
      std::make_unique<Network::Address::Ipv6Instance>("::0001", 5678));

  EXPECT_CALL(callbacks, socket()).WillOnce(Invoke([&]() -> Network::ConnectionSocket& {
    return socket;
  }));

  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&](const Network::ListenerFilterMatcherSharedPtr&,
                           Network::ListenerFilterPtr& filter) -> void {
        EXPECT_EQ(Network::FilterStatus::Continue, filter->onAccept(callbacks));
      }));

  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
  EXPECT_TRUE(socket.localAddressRestored());
  EXPECT_EQ("[1::2]:2345", socket.localAddress()->asString());
}

// Validate that when neither transparent nor freebind is not set in the
// Listener, we see no socket option set.
TEST_F(ListenerManagerImplWithRealFiltersTest, TransparentFreebindListenerDisabled) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    name: "TestListener"
    address:
      socket_address: { address: 127.0.0.1, port_value: 1111 }
    transparent: false
    freebind: false
    filter_chains:
    - filters:
  )EOF",
                                                       Network::Address::IpVersion::v4);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}))
      .WillOnce(Invoke([&](Network::Address::InstanceConstSharedPtr, Network::Socket::Type,
                           const Network::Socket::OptionsSharedPtr& options,
                           const ListenSocketCreationParams&) -> Network::SocketSharedPtr {
        EXPECT_EQ(options, nullptr);
        return listener_factory_.socket_;
      }));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

// Validate that when transparent is set in the Listener, we see the socket option
// propagated to setsockopt(). This is as close to an end-to-end test as we have
// for this feature, due to the complexity of creating an integration test
// involving the network stack. We only test the IPv4 case here, as the logic
// around IPv4/IPv6 handling is tested generically in
// socket_option_impl_test.cc.
TEST_F(ListenerManagerImplWithRealFiltersTest, TransparentListenerEnabled) {
  auto listener = createIPv4Listener("TransparentListener");
  listener.mutable_transparent()->set_value(true);
  testSocketOption(listener, envoy::config::core::v3::SocketOption::STATE_PREBIND,
                   ENVOY_SOCKET_IP_TRANSPARENT, /* expected_value */ 1,
                   /* expected_num_options */ 2);
}

// Validate that when freebind is set in the Listener, we see the socket option
// propagated to setsockopt(). This is as close to an end-to-end test as we have
// for this feature, due to the complexity of creating an integration test
// involving the network stack. We only test the IPv4 case here, as the logic
// around IPv4/IPv6 handling is tested generically in
// socket_option_impl_test.cc.
TEST_F(ListenerManagerImplWithRealFiltersTest, FreebindListenerEnabled) {
  auto listener = createIPv4Listener("FreebindListener");
  listener.mutable_freebind()->set_value(true);

  testSocketOption(listener, envoy::config::core::v3::SocketOption::STATE_PREBIND,
                   ENVOY_SOCKET_IP_FREEBIND, /* expected_value */ 1);
}

// Validate that when tcp_fast_open_queue_length is set in the Listener, we see the socket option
// propagated to setsockopt(). This is as close to an end-to-end test as we have
// for this feature, due to the complexity of creating an integration test
// involving the network stack. We only test the IPv4 case here, as the logic
// around IPv4/IPv6 handling is tested generically in
// socket_option_impl_test.cc.
TEST_F(ListenerManagerImplWithRealFiltersTest, FastOpenListenerEnabled) {
  auto listener = createIPv4Listener("FastOpenListener");
  listener.mutable_tcp_fast_open_queue_length()->set_value(1);

  testSocketOption(listener, envoy::config::core::v3::SocketOption::STATE_LISTENING,
                   ENVOY_SOCKET_TCP_FASTOPEN, /* expected_value */ 1);
}

// Validate that when reuse_port is set in the Listener, we see the socket option
// propagated to setsockopt().
TEST_F(ListenerManagerImplWithRealFiltersTest, ReusePortListenerEnabledForTcp) {
  auto listener = createIPv4Listener("ReusePortListener");
  listener.set_reuse_port(true);
  // when reuse_port is true, port should be 0 for creating the shared socket,
  // otherwise socket creation will be done on worker thread.
  listener.mutable_address()->mutable_socket_address()->set_port_value(0);
  testSocketOption(listener, envoy::config::core::v3::SocketOption::STATE_PREBIND,
                   ENVOY_SOCKET_SO_REUSEPORT, /* expected_value */ 1,
                   /* expected_num_options */ 1,
                   /* expected_creation_params */ {true, false});
}

TEST_F(ListenerManagerImplWithRealFiltersTest, ReusePortListenerDisabled) {
  auto listener = createIPv4Listener("UdpListener");
  listener.mutable_address()->mutable_socket_address()->set_protocol(
      envoy::config::core::v3::SocketAddress::UDP);
  // For UDP, verify that we fail if reuse port is false and concurrency is > 1.
  listener.set_reuse_port(false);
  server_.options_.concurrency_ = 2;

  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(listener, "", true), EnvoyException,
      "Listening on UDP when concurrency is > 1 without the SO_REUSEPORT socket option results in "
      "unstable packet proxying. Configure the reuse_port listener option or set concurrency = 1.");
  EXPECT_EQ(0, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, LiteralSockoptListenerEnabled) {
  const envoy::config::listener::v3::Listener listener = parseListenerFromV3Yaml(R"EOF(
    name: SockoptsListener
    address:
      socket_address: { address: 127.0.0.1, port_value: 1111 }
    filter_chains:
    - filters:
    socket_options: [
      # The socket goes through socket() and bind() but never listen(), so if we
      # ever saw (7, 8, 9) being applied it would cause a EXPECT_CALL failure.
      { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
      { level: 4, name: 5, int_value: 6, state: STATE_BOUND },
      { level: 7, name: 8, int_value: 9, state: STATE_LISTENING },
    ]
  )EOF");

  expectCreateListenSocket(envoy::config::core::v3::SocketOption::STATE_PREBIND,
                           /* expected_num_options */ 3);
  expectSetsockopt(
      /* expected_sockopt_level */ 1,
      /* expected_sockopt_name */ 2,
      /* expected_value */ 3);
  expectSetsockopt(
      /* expected_sockopt_level */ 4,
      /* expected_sockopt_name */ 5,
      /* expected_value */ 6);
  manager_->addOrUpdateListener(listener, "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

// Set the resolver to the default IP resolver. The address resolver logic is unit tested in
// resolver_impl_test.cc.
TEST_F(ListenerManagerImplWithRealFiltersTest, AddressResolver) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    name: AddressResolverdListener
    address:
      socket_address: { address: 127.0.0.1, port_value: 1111, resolver_name: envoy.mock.resolver }
    filter_chains:
    - filters:
  )EOF",
                                                       Network::Address::IpVersion::v4);

  NiceMock<Network::MockAddressResolver> mock_resolver;
  EXPECT_CALL(mock_resolver, resolve(_))
      .Times(2)
      .WillRepeatedly(Return(Network::Utility::parseInternetAddress("127.0.0.1", 1111, false)));
  Registry::InjectFactory<Network::Address::Resolver> register_resolver(mock_resolver);

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, CRLFilename) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
            validation_context:
              trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
              crl: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.crl" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, CRLInline) {
  const std::string crl = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.crl"));
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
            validation_context:
              trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
              crl: { inline_string: ")EOF",
                                                                    absl::CEscape(crl), R"EOF(" }
  )EOF"),
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.api_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, InvalidCRLInline) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
            validation_context:
              trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
              crl: { inline_string: "-----BEGIN X509 CRL-----\nTOTALLY_NOT_A_CRL_HERE\n-----END X509 CRL-----\n" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException, "Failed to load CRL from <inline>");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, CRLWithNoCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
            validation_context:
              crl: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.crl" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                          EnvoyException, "^Failed to load CRL from .* without trusted CA$");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, VerifySanWithNoCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
            validation_context:
              match_subject_alt_names:
                 exact: "spiffe://lyft.com/testclient"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException,
                            "SAN-based verification of peer certificates without trusted CA "
                            "is insecure and not allowed");
}

// Disabling certificate expiration checks only makes sense with a trusted CA.
TEST_F(ListenerManagerImplWithRealFiltersTest, VerifyIgnoreExpirationWithNoCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
            validation_context:
              allow_expired_certificate: true
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true),
                            EnvoyException,
                            "Certificate validity period is always ignored without trusted CA");
}

// Verify that with a CA, expired certificates are allowed.
TEST_F(ListenerManagerImplWithRealFiltersTest, VerifyIgnoreExpirationWithCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }

            validation_context:
              trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
              allow_expired_certificate: true
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_NO_THROW(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true));
}

// Validate that dispatcher stats prefix is set correctly when enabled.
TEST_F(ListenerManagerImplWithDispatcherStatsTest, DispatherStatsWithCorrectPrefix) {
  EXPECT_CALL(*worker_, start(_));
  EXPECT_CALL(*worker_, initializeStats(_));
  manager_->startWorkers(guard_dog_);
}

TEST_F(ListenerManagerImplWithRealFiltersTest, ApiListener) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  ASSERT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", false));
  EXPECT_EQ(0U, manager_->listeners().size());
  ASSERT_TRUE(manager_->apiListener().has_value());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, ApiListenerNotAllowedAddedViaApi) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  ASSERT_FALSE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true));
  EXPECT_EQ(0U, manager_->listeners().size());
  ASSERT_FALSE(manager_->apiListener().has_value());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, ApiListenerOnlyOneApiListener) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  const std::string yaml2 = R"EOF(
name: test_api_listener_2
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  ASSERT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", false));
  EXPECT_EQ(0U, manager_->listeners().size());
  ASSERT_TRUE(manager_->apiListener().has_value());
  EXPECT_EQ("test_api_listener", manager_->apiListener()->get().name());

  // Only one ApiListener is added.
  ASSERT_FALSE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", false));
  EXPECT_EQ(0U, manager_->listeners().size());
  // The original ApiListener is there.
  ASSERT_TRUE(manager_->apiListener().has_value());
  EXPECT_EQ("test_api_listener", manager_->apiListener()->get().name());
}

TEST_F(ListenerManagerImplTest, StopInplaceWarmingListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into warming.
  const std::string listener_foo_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  filter_chain_match:
    destination_port: 1234
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerOverridden(true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  EXPECT_EQ(1UL, manager_->listeners().size());

  // Stop foo which should remove warming listener.
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_factory_.socket_, close());
  EXPECT_CALL(*listener_foo, onDestroy());
  manager_->stopListeners(ListenerManager::StopListenersType::InboundOnly);
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_stopped").value());
}

TEST_F(ListenerManagerImplTest, RemoveInplaceUpdatingListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into warming.
  const std::string listener_foo_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  filter_chain_match:
    destination_port: 1234
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerOverridden(true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 1, 0, 1, 1, 0, 0);

  // Remove foo which should remove both warming and active.
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_factory_.socket_, close());
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(__LINE__, 1, 1, 1, 0, 0, 1, 0);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(__LINE__, 1, 1, 1, 0, 0, 1, 0);
  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 1, 1, 0, 0, 0, 0);
}

TEST_F(ListenerManagerImplTest, UpdateInplaceWarmingListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into warming.
  const std::string listener_foo_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  filter_chain_match:
    destination_port: 1234
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerOverridden(true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 1, 0, 1, 1, 0, 0);

  // Listener warmed up.
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_CALL(*listener_foo, onDestroy());
  listener_foo_update1->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());

  EXPECT_CALL(*listener_foo_update1, onDestroy());
}

TEST_F(ListenerManagerImplTest, DrainageDuringInplaceUpdate) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into warming.
  const std::string listener_foo_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
traffic_direction: INBOUND
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  filter_chain_match:
    destination_port: 1234
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerOverridden(true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1UL, manager_->listeners().size());
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());
  checkStats(__LINE__, 1, 1, 0, 1, 1, 0, 0);

  // The warmed up starts the drain timer.
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_CALL(server_.options_, drainTime()).WillOnce(Return(std::chrono::seconds(600)));
  Event::MockTimer* filter_chain_drain_timer = new Event::MockTimer(&server_.dispatcher_);
  EXPECT_CALL(*filter_chain_drain_timer, enableTimer(std::chrono::milliseconds(600000), _));
  listener_foo_update1->target_.ready();
  checkStats(__LINE__, 1, 1, 0, 0, 1, 0, 1);

  // Timer expires, worker close connections if any.
  EXPECT_CALL(*worker_, removeFilterChains(_, _, _));
  filter_chain_drain_timer->invokeCallback();

  // Once worker clean up is done, it's safe for the main thread to remove the original listener.
  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callDrainFilterChainsComplete();
  checkStats(__LINE__, 1, 1, 0, 0, 1, 0, 0);

  EXPECT_CALL(*listener_foo_update1, onDestroy());
}

TEST(ListenerMessageUtilTest, ListenerMessageSameAreEquivalent) {
  envoy::config::listener::v3::Listener listener1;
  envoy::config::listener::v3::Listener listener2;
  EXPECT_TRUE(Server::ListenerMessageUtil::filterChainOnlyChange(listener1, listener2));
}

TEST(ListenerMessageUtilTest, ListenerMessageHaveDifferentNameNotEquivalent) {
  envoy::config::listener::v3::Listener listener1;
  listener1.set_name("listener1");
  envoy::config::listener::v3::Listener listener2;
  listener2.set_name("listener2");
  EXPECT_FALSE(Server::ListenerMessageUtil::filterChainOnlyChange(listener1, listener2));
}

TEST(ListenerMessageUtilTest, ListenerDefaultFilterChainChangeIsAlwaysFilterChainOnlyChange) {
  envoy::config::listener::v3::Listener listener1;
  listener1.set_name("common");
  envoy::config::listener::v3::FilterChain default_filter_chain_1;
  default_filter_chain_1.set_name("127.0.0.1");
  envoy::config::listener::v3::Listener listener2;
  listener2.set_name("common");
  envoy::config::listener::v3::FilterChain default_filter_chain_2;
  default_filter_chain_2.set_name("127.0.0.2");

  {
    listener1.clear_default_filter_chain();
    listener2.clear_default_filter_chain();
    EXPECT_TRUE(Server::ListenerMessageUtil::filterChainOnlyChange(listener1, listener2));
  }
  {
    *listener1.mutable_default_filter_chain() = default_filter_chain_1;
    listener2.clear_default_filter_chain();
    EXPECT_TRUE(Server::ListenerMessageUtil::filterChainOnlyChange(listener1, listener2));
  }
  {
    listener1.clear_default_filter_chain();
    *listener2.mutable_default_filter_chain() = default_filter_chain_2;
    EXPECT_TRUE(Server::ListenerMessageUtil::filterChainOnlyChange(listener1, listener2));
  }
  {
    *listener1.mutable_default_filter_chain() = default_filter_chain_1;
    *listener2.mutable_default_filter_chain() = default_filter_chain_2;
    EXPECT_TRUE(Server::ListenerMessageUtil::filterChainOnlyChange(listener1, listener2));
  }
}

TEST(ListenerMessageUtilTest, ListenerMessageHaveDifferentFilterChainsAreEquivalent) {
  envoy::config::listener::v3::Listener listener1;
  listener1.set_name("common");
  auto add_filter_chain_1 = listener1.add_filter_chains();
  add_filter_chain_1->set_name("127.0.0.1");

  envoy::config::listener::v3::Listener listener2;
  listener2.set_name("common");
  auto add_filter_chain_2 = listener2.add_filter_chains();
  add_filter_chain_2->set_name("127.0.0.2");

  EXPECT_TRUE(Server::ListenerMessageUtil::filterChainOnlyChange(listener1, listener2));
}

TEST_F(ListenerManagerImplForInPlaceFilterChainUpdateTest, TraditionalUpdateIfWorkerNotStarted) {
  // Worker is not started yet.
  auto listener_proto = createDefaultListener();
  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));
  manager_->addOrUpdateListener(listener_proto, "", true);
  EXPECT_EQ(1u, manager_->listeners().size());

  // Mutate the listener message as filter chain change only.
  auto new_listener_proto = listener_proto;
  new_listener_proto.mutable_filter_chains(0)
      ->mutable_filter_chain_match()
      ->mutable_destination_port()
      ->set_value(9999);

  EXPECT_CALL(*listener_foo, onDestroy());
  ListenerHandle* listener_foo_update1 = expectListenerCreate(false, true);
  manager_->addOrUpdateListener(new_listener_proto, "", true);
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());
}

TEST_F(ListenerManagerImplForInPlaceFilterChainUpdateTest, TraditionalUpdateIfAnyListenerIsNotTcp) {
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  auto listener_proto = createDefaultListener();

  ListenerHandle* listener_foo = expectListenerCreate(false, true);

  expectAddListener(listener_proto, listener_foo);

  auto new_listener_proto = listener_proto;
  new_listener_proto.mutable_address()->mutable_socket_address()->set_protocol(
      envoy::config::core::v3::SocketAddress_Protocol::SocketAddress_Protocol_UDP);

  ListenerHandle* listener_foo_update1 = expectListenerCreate(false, true);
  expectUpdateToThenDrain(new_listener_proto, listener_foo);

  expectRemove(new_listener_proto, listener_foo_update1);

  EXPECT_EQ(0UL, manager_->listeners().size());
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());
}

TEST_F(ListenerManagerImplForInPlaceFilterChainUpdateTest,
       TraditionalUpdateIfImplicitTlsInspectorChanges) {

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  auto listener_proto = createDefaultListener();

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  expectAddListener(listener_proto, listener_foo);

  ListenerHandle* listener_foo_update1 = expectListenerCreate(false, true);

  auto new_listener_proto = listener_proto;
  *new_listener_proto.mutable_filter_chains(0)
       ->mutable_filter_chain_match()
       ->mutable_application_protocols()
       ->Add() = "alpn";
  expectUpdateToThenDrain(new_listener_proto, listener_foo);

  expectRemove(new_listener_proto, listener_foo_update1);

  EXPECT_EQ(0UL, manager_->listeners().size());
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());
}

TEST_F(ListenerManagerImplForInPlaceFilterChainUpdateTest,
       TraditionalUpdateIfImplicitProxyProtocolChanges) {

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  auto listener_proto = createDefaultListener();

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  expectAddListener(listener_proto, listener_foo);

  ListenerHandle* listener_foo_update1 = expectListenerCreate(false, true);

  auto new_listener_proto = listener_proto;
  new_listener_proto.mutable_filter_chains(0)->mutable_use_proxy_proto()->set_value(true);

  expectUpdateToThenDrain(new_listener_proto, listener_foo);
  expectRemove(new_listener_proto, listener_foo_update1);
  EXPECT_EQ(0UL, manager_->listeners().size());
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());
}

TEST_F(ListenerManagerImplForInPlaceFilterChainUpdateTest, TraditionalUpdateOnZeroFilterChain) {
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  auto listener_proto = createDefaultListener();

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  expectAddListener(listener_proto, listener_foo);

  auto new_listener_proto = listener_proto;
  new_listener_proto.clear_filter_chains();
  EXPECT_CALL(server_.validation_context_, staticValidationVisitor()).Times(0);
  EXPECT_CALL(server_.validation_context_, dynamicValidationVisitor());
  EXPECT_CALL(listener_factory_, createDrainManager_(_));
  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(new_listener_proto, "", true),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': no filter chains specified");

  expectRemove(listener_proto, listener_foo);
  EXPECT_EQ(0UL, manager_->listeners().size());
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());
}

TEST_F(ListenerManagerImplForInPlaceFilterChainUpdateTest,
       TraditionalUpdateIfListenerConfigHasUpdateOtherThanFilterChain) {
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  auto listener_proto = createDefaultListener();

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  expectAddListener(listener_proto, listener_foo);

  ListenerHandle* listener_foo_update1 = expectListenerCreate(false, true);

  auto new_listener_proto = listener_proto;
  new_listener_proto.set_traffic_direction(::envoy::config::core::v3::TrafficDirection::INBOUND);
  expectUpdateToThenDrain(new_listener_proto, listener_foo);

  expectRemove(new_listener_proto, listener_foo_update1);

  EXPECT_EQ(0UL, manager_->listeners().size());
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());
}

// This test execute an in place update first, then a traditional listener update.
// The second update is enforced by runtime.
TEST_F(ListenerManagerImplTest, RuntimeDisabledInPlaceUpdateFallbacksToTraditionalUpdate) {
  InSequence s;
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener.
  const std::string listener_foo_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, {true}));

  EXPECT_CALL(*worker_, addListener(_, _, _));

  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_yaml), "", true));
  EXPECT_EQ(0, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

  // Add foo listener again. Will execute in place filter chain update path.
  const std::string listener_foo_update1_yaml = R"EOF(
  name: foo
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 1234
  filter_chains:
  - filters: []
    filter_chain_match:
      destination_port: 1234
    )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerOverridden(false, listener_foo);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  auto* timer = new Event::MockTimer(dynamic_cast<Event::MockDispatcher*>(&server_.dispatcher()));
  EXPECT_CALL(*timer, enableTimer(_, _));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  EXPECT_EQ(1UL, manager_->listeners().size());
  worker_->callAddCompletion(true);

  EXPECT_CALL(*worker_, removeFilterChains(_, _, _));
  timer->invokeCallback();
  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callDrainFilterChainsComplete();

  // Update foo again. This time we disable in place filter chain update in runtime.
  // The traditional full listener update path is used.
  auto in_place_update_disabled_guard = disableInplaceUpdateForThisTest();
  const std::string listener_foo_update2_yaml = R"EOF(
  name: foo
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 1234
  filter_chains:
  - filters:
    filter_chain_match:
      destination_port: 2345
    )EOF";

  ListenerHandle* listener_foo_update2 = expectListenerCreate(false, true);
  EXPECT_CALL(*worker_, addListener(_, _, _));
  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_foo_update1->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV3Yaml(listener_foo_update2_yaml), "", true));
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_in_place_updated").value());

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo_update1->drain_manager_->drain_sequence_completion_();

  EXPECT_CALL(*listener_foo_update1, onDestroy());
  worker_->callRemovalCompletion();

  EXPECT_CALL(*worker_, stopListener(_, _));
  EXPECT_CALL(*listener_factory_.socket_, close());
  EXPECT_CALL(*listener_foo_update2->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo_update2->drain_manager_->drain_sequence_completion_();

  EXPECT_CALL(*listener_foo_update2, onDestroy());
  worker_->callRemovalCompletion();
  EXPECT_EQ(0UL, manager_->listeners().size());
}

// This test verifies that on default initialization the UDP Packet Writer
// is initialized in passthrough mode. (i.e. by using UdpDefaultWriter).
TEST_F(ListenerManagerImplTest, UdpDefaultWriterConfig) {
  const envoy::config::listener::v3::Listener listener = parseListenerFromV3Yaml(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    protocol: UDP
    port_value: 1234
filter_chains:
  filters: []
    )EOF");
  manager_->addOrUpdateListener(listener, "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
  Network::SocketSharedPtr listen_socket =
      manager_->listeners().front().get().listenSocketFactory().getListenSocket();
  Network::UdpPacketWriterPtr udp_packet_writer =
      manager_->listeners().front().get().udpPacketWriterFactory()->get().createUdpPacketWriter(
          listen_socket->ioHandle(), manager_->listeners()[0].get().listenerScope());
  EXPECT_FALSE(udp_packet_writer->isBatchMode());
}

TEST_F(ListenerManagerImplTest, TcpBacklogCustomConfig) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    name: TcpBacklogConfigListener
    address:
      socket_address: { address: 127.0.0.1, port_value: 1111 }
    tcp_backlog_size: 100
    filter_chains:
    - filters:
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, _));
  manager_->addOrUpdateListener(parseListenerFromV3Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
  EXPECT_EQ(100U, manager_->listeners().back().get().tcpBacklogSize());
}

} // namespace
} // namespace Server
} // namespace Envoy
