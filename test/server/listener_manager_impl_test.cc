#include "test/server/listener_manager_impl_test.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/config/metadata.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/listener/original_dst/original_dst.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/server/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"

using testing::AtLeast;
using testing::InSequence;
using testing::Throw;

namespace Envoy {
namespace Server {
namespace {

class ListenerManagerImplWithRealFiltersTest : public ListenerManagerImplTest {
public:
  /**
   * Create an IPv4 listener with a given name.
   */
  envoy::api::v2::Listener createIPv4Listener(const std::string& name) {
    envoy::api::v2::Listener listener = parseListenerFromV2Yaml(R"EOF(
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
  void testSocketOption(const envoy::api::v2::Listener& listener,
                        const envoy::api::v2::core::SocketOption::SocketState& expected_state,
                        const Network::SocketOptionName& expected_option, int expected_value,
                        uint32_t expected_num_options = 1) {
    if (expected_option.has_value()) {
      expectCreateListenSocket(expected_state, expected_num_options);
      expectSetsockopt(os_sys_calls_, expected_option.level(), expected_option.option(),
                       expected_value, expected_num_options);
      manager_->addOrUpdateListener(listener, "", true);
      EXPECT_EQ(1U, manager_->listeners().size());
    } else {
      EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(listener, "", true), EnvoyException,
                                "MockListenerComponentFactory: Setting socket options failed");
      EXPECT_EQ(0U, manager_->listeners().size());
    }
  }
};

class MockLdsApi : public LdsApi {
public:
  MOCK_CONST_METHOD0(versionInfo, std::string());
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

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(8192U, manager_->listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SslContext) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters: []
  tls_context:
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

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, UdpAddress) {
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
  envoy::api::v2::Listener listener_proto;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(proto_text, &listener_proto));

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_,
              createListenSocket(_, Network::Address::SocketType::Datagram, _, true));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, _, _, _, _)).Times(testing::AtLeast(1));
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

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                          EnvoyException, "test: Cannot find field");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadListenerConfigNoFilterChains) {
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
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
- name: envoy.listener.tls_inspector
- name: envoy.listener.original_dst
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                          EnvoyException, "Only 1 UDP filter per listener supported");
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
    config: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                          EnvoyException, "foo: Cannot find field");
}
class NonTerminalFilterFactory : public Configuration::NamedNetworkFilterConfigFactory {
public:
  // Configuration::NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb createFilterFactory(const Json::Object&,
                                               Configuration::FactoryContext&) override {
    return [](Network::FilterManager&) -> void {};
  }

  Network::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message&,
                                                        Configuration::FactoryContext&) override {
    return [](Network::FilterManager&) -> void {};
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Empty>();
  }

  std::string name() override { return "non_terminal"; }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, TerminalNotLast) {
  Registry::RegisterFactory<NonTerminalFilterFactory,
                            Configuration::NamedNetworkFilterConfigFactory>
      registered;
  const std::string yaml = R"EOF(
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  - name: non_terminal
    config: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                          EnvoyException,
                          "Error: non-terminal filter non_terminal is the last "
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
  - name: envoy.tcp_proxy
    config: {}
  - name: unknown_but_will_not_be_processed
    config: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                          EnvoyException,
                          "Error: envoy.tcp_proxy must be the terminal network filter.");
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
    config: {}
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException,
                            "Didn't find a registered implementation for name: 'invalid'");
}

class TestStatsConfigFactory : public Configuration::NamedNetworkFilterConfigFactory {
public:
  // Configuration::NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb createFilterFactory(const Json::Object&,
                                               Configuration::FactoryContext& context) override {
    return commonFilterFactory(context);
  }

  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Configuration::FactoryContext& context) override {
    return commonFilterFactory(context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Empty>();
  }

  std::string name() override { return "stats_test"; }
  bool isTerminalFilter() override { return true; }

private:
  Network::FilterFactoryCb commonFilterFactory(Configuration::FactoryContext& context) {
    context.scope().counter("bar").inc();
    return [](Network::FilterManager&) -> void {};
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, StatsScopeTest) {
  Registry::RegisterFactory<TestStatsConfigFactory, Configuration::NamedNetworkFilterConfigFactory>
      registered;

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
    config: {}
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, false));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  manager_->listeners().front().get().listenerScope().counter("foo").inc();

  EXPECT_EQ(1UL, server_.stats_store_.counter("bar").value());
  EXPECT_EQ(1UL, server_.stats_store_.counter("listener.127.0.0.1_1234.foo").value());
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

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true));
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
      expectListenerCreate(false, true, envoy::api::v2::Listener_DrainType_MODIFY_ONLY);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(1, 0, 0, 0, 1, 0);

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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(1, 0, 0, 0, 1, 0);

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
      expectListenerCreate(false, true, envoy::api::v2::Listener_DrainType_MODIFY_ONLY);
  EXPECT_CALL(*listener_foo_different_address, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_different_address_yaml),
                                    "", true),
      EnvoyException,
      "error updating listener: 'foo' has a different address "
      "'127.0.0.1:1235' from existing listener");

  EXPECT_CALL(*listener_foo, onDestroy());
}

// Make sure that a listener creation does not fail on IPv4 only setups when FilterChainMatch is not
// specified and we try to create default CidrRange. See makeCidrListEntry function for
// more details.
TEST_F(ListenerManagerImplTest, AddListenerOnIpv4OnlySetups) {
  InSequence s;

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

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

  ON_CALL(os_sys_calls, socket(AF_INET, _, 0)).WillByDefault(Return(Api::SysCallIntResult{5, 0}));
  ON_CALL(os_sys_calls, socket(AF_INET6, _, 0)).WillByDefault(Return(Api::SysCallIntResult{-1, 0}));
  ON_CALL(os_sys_calls, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));

  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(1, 0, 0, 0, 1, 0);
  EXPECT_CALL(*listener_foo, onDestroy());
}

// Make sure that a listener creation does not fail on IPv6 only setups when FilterChainMatch is not
// specified and we try to create default CidrRange. See makeCidrListEntry function for
// more details.
TEST_F(ListenerManagerImplTest, AddListenerOnIpv6OnlySetups) {
  InSequence s;

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

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

  ON_CALL(os_sys_calls, socket(AF_INET, _, 0)).WillByDefault(Return(Api::SysCallIntResult{-1, 0}));
  ON_CALL(os_sys_calls, socket(AF_INET6, _, 0)).WillByDefault(Return(Api::SysCallIntResult{5, 0}));
  ON_CALL(os_sys_calls, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));

  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(1, 0, 0, 0, 1, 0);
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", false));
  checkStats(1, 0, 0, 0, 1, 0);
  checkConfigDump(R"EOF(
static_listeners:
  listener:
    name: "foo"
    address:
      socket_address:
        address: "127.0.0.1"
        port_value: 1234
    filter_chains: {}
  last_updated:
    seconds: 1001001001
    nanos: 1000000
dynamic_active_listeners:
dynamic_warming_listeners:
dynamic_draining_listeners:
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
    config: {}
  )EOF";

  EXPECT_FALSE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_update1_yaml), "", false));
  checkStats(1, 0, 0, 0, 1, 0);

  // Remove foo listener. Should be blocked.
  EXPECT_FALSE(manager_->removeListener("foo"));
  checkStats(1, 0, 0, 0, 1, 0);

  EXPECT_CALL(*listener_foo, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddOrUpdateListener) {
  time_system_.setSystemTime(std::chrono::milliseconds(1001001001001));

  InSequence s;

  auto* lds_api = new MockLdsApi();
  EXPECT_CALL(listener_factory_, createLdsApi_(_)).WillOnce(Return(lds_api));
  envoy::api::v2::core::ConfigSource lds_config;
  manager_->createLdsApi(lds_config);

  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return(""));
  checkConfigDump(R"EOF(
static_listeners:
dynamic_active_listeners:
dynamic_warming_listeners:
dynamic_draining_listeners:
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "version1", true));
  checkStats(1, 0, 0, 0, 1, 0);
  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version1"));
  checkConfigDump(R"EOF(
version_info: version1
static_listeners:
dynamic_active_listeners:
dynamic_warming_listeners:
  version_info: "version1"
  listener:
    name: "foo"
    address:
      socket_address:
        address: "127.0.0.1"
        port_value: 1234
    filter_chains: {}
  last_updated:
    seconds: 1001001001
    nanos: 1000000
dynamic_draining_listeners:
)EOF");

  // Update duplicate should be a NOP.
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(1, 0, 0, 0, 1, 0);

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
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_update1_yaml),
                                            "version2", true));
  checkStats(1, 1, 0, 0, 1, 0);
  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version2"));
  checkConfigDump(R"EOF(
version_info: version2
static_listeners:
dynamic_active_listeners:
dynamic_warming_listeners:
  version_info: "version2"
  listener:
    name: "foo"
    address:
      socket_address:
        address: "127.0.0.1"
        port_value: 1234
    filter_chains: {}
    per_connection_buffer_limit_bytes: 10
  last_updated:
    seconds: 2002002002
    nanos: 2000000
dynamic_draining_listeners:
)EOF");

  // Start workers.
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);
  worker_->callAddCompletion(true);

  // Update duplicate should be a NOP.
  EXPECT_FALSE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_update1_yaml), "", true));
  checkStats(1, 1, 0, 0, 1, 0);

  time_system_.setSystemTime(std::chrono::milliseconds(3003003003003));

  // Update foo. Should go into warming, have an immediate warming callback, and start immediate
  // removal.
  ListenerHandle* listener_foo_update2 = expectListenerCreate(false, true);
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo_update1->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "version3", true));
  worker_->callAddCompletion(true);
  checkStats(1, 2, 0, 0, 1, 1);
  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version3"));
  checkConfigDump(R"EOF(
version_info: version3
static_listeners:
dynamic_active_listeners:
  version_info: "version3"
  listener:
    name: "foo"
    address:
      socket_address:
        address: "127.0.0.1"
        port_value: 1234
    filter_chains: {}
  last_updated:
    seconds: 3003003003
    nanos: 3000000
dynamic_warming_listeners:
dynamic_draining_listeners:
  version_info: "version2"
  listener:
    name: "foo"
    address:
      socket_address:
        address: "127.0.0.1"
        port_value: 1234
    filter_chains: {}
    per_connection_buffer_limit_bytes: 10
  last_updated:
    seconds: 2002002002
    nanos: 2000000
)EOF");

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo_update1->drain_manager_->drain_sequence_completion_();
  checkStats(1, 2, 0, 0, 1, 1);
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  worker_->callRemovalCompletion();
  checkStats(1, 2, 0, 0, 1, 0);

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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_bar_yaml), "version4", true));
  EXPECT_EQ(2UL, manager_->listeners().size());
  worker_->callAddCompletion(true);
  checkStats(2, 2, 0, 0, 2, 0);

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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(listener_baz->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_baz_yaml), "version5", true));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(3, 2, 0, 1, 2, 0);
  EXPECT_CALL(*lds_api, versionInfo()).WillOnce(Return("version5"));
  checkConfigDump(R"EOF(
version_info: version5
static_listeners:
dynamic_active_listeners:
  - version_info: "version3"
    listener:
      name: "foo"
      address:
        socket_address:
          address: "127.0.0.1"
          port_value: 1234
      filter_chains: {}
    last_updated:
      seconds: 3003003003
      nanos: 3000000
  - version_info: "version4"
    listener:
      name: "bar"
      address:
        socket_address:
          address: "127.0.0.1"
          port_value: 1235
      filter_chains: {}
    last_updated:
      seconds: 4004004004
      nanos: 4000000
dynamic_warming_listeners:
  - version_info: "version5"
    listener:
      name: "baz"
      address:
        socket_address:
          address: "127.0.0.1"
          port_value: 1236
      filter_chains: {}
    last_updated:
      seconds: 5005005005
      nanos: 5000000
dynamic_draining_listeners:
)EOF");

  // Update a duplicate baz that is currently warming.
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_baz_yaml), "", true));
  checkStats(3, 2, 0, 1, 2, 0);

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
    config: {}
  )EOF";

  ListenerHandle* listener_baz_update1 = expectListenerCreate(true, true);
  EXPECT_CALL(*listener_baz, onDestroy()).WillOnce(Invoke([listener_baz]() -> void {
    // Call the initialize callback during destruction like RDS will.
    listener_baz->target_.ready();
  }));
  EXPECT_CALL(listener_baz_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_baz_update1_yaml), "", true));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(3, 3, 0, 1, 2, 0);

  // Finish initialization for baz which should make it active.
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_baz_update1->target_.ready();
  EXPECT_EQ(3UL, manager_->listeners().size());
  worker_->callAddCompletion(true);
  checkStats(3, 3, 0, 0, 3, 0);

  EXPECT_CALL(*listener_foo_update2, onDestroy());
  EXPECT_CALL(*listener_bar, onDestroy());
  EXPECT_CALL(*listener_baz_update1, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddDrainingListener) {
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  worker_->callAddCompletion(true);
  checkStats(1, 0, 0, 0, 1, 0);

  // Remove foo into draining.
  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(1, 0, 1, 0, 0, 1);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(1, 0, 1, 0, 0, 1);

  // Add foo again. We should use the socket from draining.
  ListenerHandle* listener_foo2 = expectListenerCreate(false, true);
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  worker_->callAddCompletion(true);
  checkStats(2, 0, 1, 0, 1, 1);

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  checkStats(2, 0, 1, 0, 1, 0);

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
  Api::OsSysCallsImpl os_syscall;
  auto syscall_result = os_syscall.socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(syscall_result.rc_, 0);
  ListenerHandle* listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, false))
      .WillOnce(Invoke([this, &syscall_result, &real_listener_factory](
                           const Network::Address::InstanceConstSharedPtr& address,
                           Network::Address::SocketType socket_type,
                           const Network::Socket::OptionsSharedPtr& options,
                           bool bind_to_port) -> Network::SocketSharedPtr {
        EXPECT_CALL(server_, hotRestart).Times(0);
        // When bind_to_port is equal to false, create socket fd directly, and do not get socket
        // fd through hot restart.
        NiceMock<Api::MockOsSysCalls> os_sys_calls;
        TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
        ON_CALL(os_sys_calls, socket(AF_INET, _, 0))
            .WillByDefault(Return(Api::SysCallIntResult{syscall_result.rc_, 0}));
        return real_listener_factory.createListenSocket(address, socket_type, options,
                                                        bind_to_port);
      }));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
}

TEST_F(ListenerManagerImplTest, NotSupportedDatagramUds) {
  ProdListenerComponentFactory real_listener_factory(server_);
  EXPECT_THROW_WITH_MESSAGE(real_listener_factory.createListenSocket(
                                std::make_shared<Network::Address::PipeInstance>("/foo"),
                                Network::Address::SocketType::Datagram, nullptr, true),
                            EnvoyException,
                            "socket type SocketType::Datagram not supported for pipes");
}

TEST_F(ListenerManagerImplTest, CantBindSocket) {
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true))
      .WillOnce(Throw(EnvoyException("can't bind")));
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_THROW(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true),
               EnvoyException);
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  worker_->callAddCompletion(true);
  checkStats(1, 0, 0, 0, 1, 0);

  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(server_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_FALSE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(1, 0, 1, 0, 0, 1);

  // NOTE: || short circuit here prevents the server drain manager from getting called.
  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(true));
  EXPECT_TRUE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(1, 0, 1, 0, 0, 1);

  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(server_.drain_manager_, drainClose()).WillOnce(Return(true));
  EXPECT_TRUE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(1, 0, 1, 0, 0, 0);
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(1, 0, 0, 1, 0, 0);

  // Remove foo.
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->removeListener("foo"));
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(1, 0, 1, 0, 0, 0);

  // Add foo again and initialize it.
  listener_foo = expectListenerCreate(true, true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(2, 0, 1, 1, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(2, 0, 1, 0, 1, 0);

  // Update foo into warming.
  const std::string listener_foo_update1_yaml = R"EOF(
name: foo
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
filter_chains:
- filters:
  - name: fake
    config: {}
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerCreate(true, true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(2, 1, 1, 1, 1, 0);

  // Remove foo which should remove both warming and active.
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(2, 1, 2, 0, 0, 1);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(2, 1, 2, 0, 0, 1);
  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(2, 1, 2, 0, 0, 0);
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(1, 0, 0, 1, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(1, 0, 0, 0, 1, 0);

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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(listener_foo_outbound->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_outbound_yaml), "", true));
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_foo_outbound->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(2UL, manager_->listeners().size());

  // Validate that stop listener is only called once - for inbound listeners.
  EXPECT_CALL(*worker_, stopListener(_)).Times(1);
  manager_->stopListeners(ListenerManager::StopListenersType::InboundOnly);
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_stopped").value());

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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_bar_outbound_yaml), "", true));
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
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_bar_yaml), "", true));
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(1, 0, 0, 1, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(1, 0, 0, 0, 1, 0);

  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo, onDestroy());
  manager_->stopListeners(ListenerManager::StopListenersType::All);
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_stopped").value());

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
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_bar_yaml), "", true));
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));
  checkStats(1, 0, 0, 1, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(1, 0, 0, 0, 1, 0);

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
  - name: fake
    config: {}
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerCreate(true, true);
  EXPECT_CALL(listener_foo_update1->target_, initialize());
  EXPECT_TRUE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_update1_yaml), "", true));
  EXPECT_EQ(1UL, manager_->listeners().size());

  // Stop foo which should remove warming listener.
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo, onDestroy());
  manager_->stopListeners(ListenerManager::StopListenersType::InboundOnly);
  EXPECT_EQ(1, server_.stats_store_.counter("listener_manager.listener_stopped").value());
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));

  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  worker_->callAddCompletion(false);

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();

  EXPECT_EQ(1UL, server_.stats_store_.counter("listener_manager.listener_create_failure").value());
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

  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  manager_->listeners().front().get().listenerScope().counter("foo").inc();

  EXPECT_EQ(1UL, server_.stats_store_.counter("listener.[__1]_10000.foo").value());
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
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, false));
  EXPECT_CALL(listener_foo->target_, initialize());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml), "", true));

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
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_bar_yaml), "", true),
      EnvoyException,
      "error adding listener: 'bar' has duplicate address '0.0.0.0:1234' as existing listener");

  // Move foo to active and then try to add again. This should still fail.
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_foo->target_.ready();
  worker_->callAddCompletion(true);

  listener_bar = expectListenerCreate(true, true);
  EXPECT_CALL(*listener_bar, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_bar_yaml), "", true),
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        destination_port: 8080
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        prefix_ranges: { address_prefix: 127.0.0.0, prefix_len: 8 }
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "server1.example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        transport_protocol: "tls"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        application_protocols: "http/1.1"
        source_type: ANY
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS client without ALPN - no match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // TLS client with "http/1.1" ALPN - using 1st filter chain.
  filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {"h2", "http/1.1"}, "8.8.8.8", 111);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        source_type: LOCAL
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // EXTERNAL IPv4 client without "http/1.1" ALPN - no match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // LOCAL IPv4 client with "http/1.1" ALPN - using 1st filter chain.
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "", "tls", {"h2", "http/1.1"}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 1);
  EXPECT_EQ(server_names.front(), "server1.example.com");

  // LOCAL UDS client with "http/1.1" ALPN - using 1st filter chain.
  filter_chain =
      findFilterChain(0, "/tmp/test.sock", "", "tls", {"h2", "http/1.1"}, "/tmp/test.sock", 111);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        source_prefix_ranges:
          - address_prefix: 10.0.0.1
            prefix_len: 24
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // IPv4 client with source 10.0.1.1. No match.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "10.0.1.1", 111);
  EXPECT_EQ(filter_chain, nullptr);

  // IPv4 client with source 10.0.0.10, Match.
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "", "tls", {"h2", "http/1.1"}, "10.0.0.10", 111);
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
  filter_chain =
      findFilterChain(0, "/tmp/test.sock", "", "tls", {"h2", "http/1.1"}, "/tmp/test.sock", 0);
  ASSERT_EQ(filter_chain, nullptr);
}

// Verify source IPv6 matches.
TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithSourceIpv6Match) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        source_prefix_ranges:
          - address_prefix: 2001:0db8:85a3:0000:0000:0000:0000:0000
            prefix_len: 64
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        source_ports:
          - 100
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
  filter_chain = findFilterChain(1234, "8.8.8.8", "", "tls", {"h2", "http/1.1"}, "4.4.4.4", 101);
  ASSERT_EQ(filter_chain, nullptr);
}

// Define multiple source_type filter chain matches and test against them.
TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainWithSourceTypeMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        source_type: LOCAL
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
    - filter_chain_match:
        application_protocols: "http/1.1"
        source_type: EXTERNAL
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem" }
    - filter_chain_match:
        source_type: ANY
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // LOCAL TLS client with "http/1.1" ALPN - no match.
  auto filter_chain =
      findFilterChain(1234, "127.0.0.1", "", "tls", {"h2", "http/1.1"}, "127.0.0.1", 111);
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
  filter_chain = findFilterChain(1234, "8.8.8.8", "", "tls", {"h2", "http/1.1"}, "4.4.4.4", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  auto uri = ssl_socket->ssl()->uriSanLocalCertificate();
  EXPECT_EQ(uri[0], "spiffe://lyft.com/test-team");

  // EXTERNAL TLS client without "http/1.1" ALPN - using 3nd filter chain.
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
    - filter_chain_match:
        destination_port: 8080
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
    - filter_chain_match:
        destination_port: 8081
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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

  // IPv4 client connects to port 8081 - using 3nd filter chain.
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
    - filter_chain_match:
        prefix_ranges: { address_prefix: 192.168.0.1, prefix_len: 32 }
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
    - filter_chain_match:
        prefix_ranges: { address_prefix: 192.168.0.0, prefix_len: 16 }
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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

  // IPv4 client connects to wildcard IP match - using 3nd filter chain.
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
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filter_chain_match:
        server_names: "server1.example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filter_chain_match:
        server_names: "*.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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

  // TLS client with wildcard SNI match - using 3nd filter chain.
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "server2.example.com", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_TRUE(filter_chain->transportSocketFactory().implementsSecureTransport());
  transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ssl_socket = dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  server_names = ssl_socket->ssl()->dnsSansLocalCertificate();
  EXPECT_EQ(server_names.size(), 2);
  EXPECT_EQ(server_names.front(), "*.example.com");

  // TLS client with wildcard SNI match - using 3nd filter chain.
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        # empty
    - filter_chain_match:
        transport_protocol: "tls"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        # empty
    - filter_chain_match:
        application_protocols: ["dummy", "h2"]
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS client without ALPN - using 1st filter chain.
  auto filter_chain = findFilterChain(1234, "127.0.0.1", "", "tls", {}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_FALSE(filter_chain->transportSocketFactory().implementsSecureTransport());

  // TLS client with "h2,http/1.1" ALPN - using 2nd filter chain.
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "", "tls", {"h2", "http/1.1"}, "127.0.0.1", 111);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        # empty
    - filter_chain_match:
        server_names: ["www.example.com", "server1.example.com"]
        transport_protocol: "tls"
        application_protocols: ["dummy", "h2"]
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
  filter_chain =
      findFilterChain(1234, "127.0.0.1", "", "tls", {"h2", "http/1.1"}, "127.0.0.1", 111);
  ASSERT_NE(filter_chain, nullptr);
  EXPECT_FALSE(filter_chain->transportSocketFactory().implementsSecureTransport());

  // TLS client with exact SNI match and ALPN match - using 2nd filter chain.
  filter_chain = findFilterChain(1234, "127.0.0.1", "server1.example.com", "tls",
                                 {"h2", "http/1.1"}, "127.0.0.1", 111);
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
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filter_chain_match:
        server_names: "www.example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_b"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest,
       MultipleFilterChainsWithMixedUseOfSessionTicketKeys) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filter_chain_match:
        server_names: "www.example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithInvalidDestinationIPMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        prefix_ranges: { address_prefix: a.b.c.d, prefix_len: 32 }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException, "malformed IP address: a.b.c.d");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SingleFilterChainWithInvalidServerNamesMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "*w.example.com"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': partial wildcards are not "
                            "supported in \"server_names\"");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithSameMatch) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        transport_protocol: "tls"
    - filter_chain_match:
        transport_protocol: "tls"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': multiple filter chains with "
                            "the same matching rules are defined");
}

TEST_F(ListenerManagerImplWithRealFiltersTest,
       MultipleFilterChainsWithSameMatchPlusUnimplementedFields) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        transport_protocol: "tls"
    - filter_chain_match:
        transport_protocol: "tls"
        address_suffix: 127.0.0.0
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': contains filter chains with "
                            "unimplemented fields");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, MultipleFilterChainsWithOverlappingRules) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    listener_filters:
    - name: "envoy.listener.tls_inspector"
      config: {}
    filter_chains:
    - filter_chain_match:
        server_names: "example.com"
    - filter_chain_match:
        server_names: ["example.com", "www.example.com"]
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
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

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS Inspector is automatically injected for filter chains with TLS requirements,
  // so make sure there is exactly 1 listener filter (and assume it's TLS Inspector).
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_))
      .WillOnce(Invoke([&](Network::ListenerFilterPtr&) -> void {}));
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

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS Inspector is automatically injected for filter chains with SNI requirements,
  // so make sure there is exactly 1 listener filter (and assume it's TLS Inspector).
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_))
      .WillOnce(Invoke([&](Network::ListenerFilterPtr&) -> void {}));
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

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // TLS Inspector is automatically injected for filter chains with ALPN requirements,
  // so make sure there is exactly 1 listener filter (and assume it's TLS Inspector).
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_))
      .WillOnce(Invoke([&](Network::ListenerFilterPtr&) -> void {}));
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

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  // Make sure there are no listener filters (i.e. no automatically injected TLS Inspector).
  Network::ListenerConfig& listener = manager_->listeners().back().get();
  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;
  EXPECT_CALL(manager, addAcceptFilter_(_)).Times(0);
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
    - tls_context:
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

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateChainInlinePrivateKeyFilename) {
  const std::string cert = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem"));
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem" }
              certificate_chain: { inline_string: ")EOF",
                                                                    absl::CEscape(cert), R"EOF(" }
  )EOF"),
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateIncomplete) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true), EnvoyException,
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
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { inline_string: "invalid" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
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
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { inline_string: ")EOF",
          absl::CEscape(leaf),
          R"EOF(\n-----BEGIN CERTIFICATE-----\nDEFINITELY_INVALID_CERTIFICATE\n-----END CERTIFICATE-----" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem" }
  )EOF"),
      Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException, "Failed to load certificate chain from <inline>");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateInvalidPrivateKey) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem" }
              private_key: { inline_string: "invalid" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException, "Failed to load private key from <inline>");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, TlsCertificateInvalidTrustedCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem" }
          validation_context:
              trusted_ca: { inline_string: "invalid" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
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
      - name: envoy.http_connection_manager
        config:
          stat_prefix: metadata_test
          route_config:
            virtual_hosts:
            - name: "some_virtual_host"
              domains: ["some.domain"]
              routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo }
  )EOF",
                                                       Network::Address::IpVersion::v4);
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  auto context = dynamic_cast<Configuration::FactoryContext*>(&manager_->listeners().front().get());
  ASSERT_NE(nullptr, context);
  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(context->listenerMetadata(), "com.bar.foo", "baz")
                .string_value());
  EXPECT_EQ(envoy::api::v2::core::TrafficDirection::INBOUND, context->direction());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, OriginalDstFilter) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1111 }
    filter_chains: {}
    listener_filters:
    - name: "envoy.listener.original_dst"
      config: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());

  Network::ListenerConfig& listener = manager_->listeners().back().get();

  Network::FilterChainFactory& filterChainFactory = listener.filterChainFactory();
  Network::MockListenerFilterManager manager;

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  Network::AcceptedSocketImpl socket(std::make_unique<Network::IoSocketHandleImpl>(),
                                     Network::Address::InstanceConstSharedPtr{
                                         new Network::Address::Ipv4Instance("127.0.0.1", 1234)},
                                     Network::Address::InstanceConstSharedPtr{
                                         new Network::Address::Ipv4Instance("127.0.0.1", 5678)});

  EXPECT_CALL(callbacks, socket()).WillOnce(Invoke([&]() -> Network::ConnectionSocket& {
    return socket;
  }));

  EXPECT_CALL(manager, addAcceptFilter_(_))
      .WillOnce(Invoke([&](Network::ListenerFilterPtr& filter) -> void {
        EXPECT_EQ(Network::FilterStatus::Continue, filter->onAccept(callbacks));
      }));

  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
}

class OriginalDstTestFilter : public Extensions::ListenerFilters::OriginalDst::OriginalDstFilter {
  Network::Address::InstanceConstSharedPtr getOriginalDst(int) override {
    return Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("127.0.0.2", 2345)};
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, OriginalDstTestFilter) {
  class OriginalDstTestConfigFactory : public Configuration::NamedListenerFilterConfigFactory {
  public:
    // NamedListenerFilterConfigFactory
    Network::ListenerFilterFactoryCb
    createFilterFactoryFromProto(const Protobuf::Message&,
                                 Configuration::ListenerFactoryContext&) override {
      return [](Network::ListenerFilterManager& filter_manager) -> void {
        filter_manager.addAcceptFilter(std::make_unique<OriginalDstTestFilter>());
      };
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      return std::make_unique<Envoy::ProtobufWkt::Empty>();
    }

    std::string name() override { return "test.listener.original_dst"; }
  };

  /**
   * Static registration for the original dst filter. @see RegisterFactory.
   */
  static Registry::RegisterFactory<OriginalDstTestConfigFactory,
                                   Configuration::NamedListenerFilterConfigFactory>
      registered_;

  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1111 }
    filter_chains: {}
    listener_filters:
    - name: "test.listener.original_dst"
      config: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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

  EXPECT_CALL(manager, addAcceptFilter_(_))
      .WillOnce(Invoke([&](Network::ListenerFilterPtr& filter) -> void {
        EXPECT_EQ(Network::FilterStatus::Continue, filter->onAccept(callbacks));
      }));

  EXPECT_TRUE(filterChainFactory.createListenerFilterChain(manager));
  EXPECT_TRUE(socket.localAddressRestored());
  EXPECT_EQ("127.0.0.2:2345", socket.localAddress()->asString());
}

class OriginalDstTestFilterIPv6
    : public Extensions::ListenerFilters::OriginalDst::OriginalDstFilter {
  Network::Address::InstanceConstSharedPtr getOriginalDst(int) override {
    return Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance("1::2", 2345)};
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, OriginalDstTestFilterIPv6) {
  class OriginalDstTestConfigFactory : public Configuration::NamedListenerFilterConfigFactory {
  public:
    // NamedListenerFilterConfigFactory
    Network::ListenerFilterFactoryCb
    createFilterFactoryFromProto(const Protobuf::Message&,
                                 Configuration::ListenerFactoryContext&) override {
      return [](Network::ListenerFilterManager& filter_manager) -> void {
        filter_manager.addAcceptFilter(std::make_unique<OriginalDstTestFilterIPv6>());
      };
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      return std::make_unique<Envoy::ProtobufWkt::Empty>();
    }

    std::string name() override { return "test.listener.original_dstipv6"; }
  };

  /**
   * Static registration for the original dst filter. @see RegisterFactory.
   */
  static Registry::RegisterFactory<OriginalDstTestConfigFactory,
                                   Configuration::NamedListenerFilterConfigFactory>
      registered_;

  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: ::0001, port_value: 1111 }
    filter_chains: {}
    listener_filters:
    - name: "test.listener.original_dstipv6"
      config: {}
  )EOF",
                                                       Network::Address::IpVersion::v6);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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

  EXPECT_CALL(manager, addAcceptFilter_(_))
      .WillOnce(Invoke([&](Network::ListenerFilterPtr& filter) -> void {
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
    filter_chains:
    - filters:
  )EOF",
                                                       Network::Address::IpVersion::v4);
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true))
      .WillOnce(Invoke([&](Network::Address::InstanceConstSharedPtr, Network::Address::SocketType,
                           const Network::Socket::OptionsSharedPtr& options,
                           bool) -> Network::SocketSharedPtr {
        EXPECT_EQ(options, nullptr);
        return listener_factory_.socket_;
      }));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
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
  testSocketOption(listener, envoy::api::v2::core::SocketOption::STATE_PREBIND,
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

  testSocketOption(listener, envoy::api::v2::core::SocketOption::STATE_PREBIND,
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

  testSocketOption(listener, envoy::api::v2::core::SocketOption::STATE_LISTENING,
                   ENVOY_SOCKET_TCP_FASTOPEN, /* expected_value */ 1);
}

TEST_F(ListenerManagerImplWithRealFiltersTest, LiteralSockoptListenerEnabled) {
  const envoy::api::v2::Listener listener = parseListenerFromV2Yaml(R"EOF(
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

  expectCreateListenSocket(envoy::api::v2::core::SocketOption::STATE_PREBIND,
                           /* expected_num_options */ 3);
  expectSetsockopt(os_sys_calls_,
                   /* expected_sockopt_level */ 1,
                   /* expected_sockopt_name */ 2,
                   /* expected_value */ 3);
  expectSetsockopt(os_sys_calls_,
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
      .WillOnce(Return(Network::Utility::parseInternetAddress("127.0.0.1", 1111, false)));

  Registry::InjectFactory<Network::Address::Resolver> register_resolver(mock_resolver);

  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, CRLFilename) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          validation_context:
            trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
            crl: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.crl" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, CRLInline) {
  const std::string crl = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.crl"));
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
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

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, InvalidCRLInline) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          validation_context:
            trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
            crl: { inline_string: "-----BEGIN X509 CRL-----\nTOTALLY_NOT_A_CRL_HERE\n-----END X509 CRL-----\n" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException, "Failed to load CRL from <inline>");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, CRLWithNoCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          validation_context:
            crl: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.crl" }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_REGEX(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                          EnvoyException, "^Failed to load CRL from .* without trusted CA$");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, VerifySanWithNoCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          validation_context:
            verify_subject_alt_name: "spiffe://lyft.com/testclient"
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
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
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }
          validation_context:
            allow_expired_certificate: true
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true),
                            EnvoyException,
                            "Certificate validity period is always ignored without trusted CA");
}

// Verify that with a CA, expired certificates are allowed.
TEST_F(ListenerManagerImplWithRealFiltersTest, VerifyIgnoreExpirationWithCA) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem" }

          validation_context:
            trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
            allow_expired_certificate: true
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_NO_THROW(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml), "", true));
}

} // namespace
} // namespace Server
} // namespace Envoy
