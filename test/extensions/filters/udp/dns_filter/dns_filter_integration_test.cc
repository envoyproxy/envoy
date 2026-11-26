#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/extension.pb.h"

#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter.h"
#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "dns_filter_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

using testing::NiceMock;
using ResponseValidator = Utils::DnsResponseValidator;

// Mock OS getaddrinfo.
class OsSysCallsWithMockedDns : public Api::OsSysCallsImpl {
public:
  static addrinfo* makeAddrInfo(const Network::Address::InstanceConstSharedPtr& addr) {
    addrinfo* ai = reinterpret_cast<addrinfo*>(malloc(sizeof(addrinfo)));
    memset(ai, 0, sizeof(addrinfo));
    ai->ai_protocol = IPPROTO_UDP;
    ai->ai_socktype = SOCK_DGRAM;
    if (addr->ip()->ipv4() != nullptr) {
      ai->ai_family = AF_INET;
    } else {
      ai->ai_family = AF_INET6;
    }
    sockaddr_storage* storage =
        reinterpret_cast<sockaddr_storage*>(malloc(sizeof(sockaddr_storage)));
    ai->ai_addr = reinterpret_cast<sockaddr*>(storage);
    memcpy(ai->ai_addr, addr->sockAddr(), addr->sockAddrLen());
    ai->ai_addrlen = addr->sockAddrLen();
    ai->ai_next = nullptr;
    return ai;
  }

  Api::SysCallIntResult getaddrinfo(const char* node, const char* /*service*/,
                                    const addrinfo* /*hints*/, addrinfo** res) override {
    *res = nullptr;
    if (absl::string_view{"www.google.com"} == node) {
      if (ip_version_ == Network::Address::IpVersion::v6) {
        static const Network::Address::InstanceConstSharedPtr* objectptr =
            new Network::Address::InstanceConstSharedPtr{
                new Network::Address::Ipv6Instance("2607:42:42::42:42", 0, nullptr)};
        *res = makeAddrInfo(*objectptr);
      } else {
        static const Network::Address::InstanceConstSharedPtr* objectptr =
            new Network::Address::InstanceConstSharedPtr{
                new Network::Address::Ipv4Instance("42.42.42.42", 0, nullptr)};
        *res = makeAddrInfo(*objectptr);
      }
      return {0, 0};
    }
    if (nonexisting_addresses_.find(node) != nonexisting_addresses_.end()) {
      return {EAI_NONAME, 0};
    }
    std::cerr << "Mock DNS does not have entry for: " << node << std::endl;
    return {-1, 128};
  }
  void freeaddrinfo(addrinfo* ai) override {
    while (ai != nullptr) {
      addrinfo* p = ai;
      ai = ai->ai_next;
      free(p->ai_addr);
      free(p);
    }
  }

  void setIpVersion(Network::Address::IpVersion version) { ip_version_ = version; }
  Network::Address::IpVersion ip_version_ = Network::Address::IpVersion::v4;
  absl::flat_hash_set<absl::string_view> nonexisting_addresses_ = {"doesnotexist.example.com",
                                                                   "itdoesnotexist"};
};

class DnsFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public BaseIntegrationTest {
public:
  DnsFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), configToUse()), api_(Api::createApiForTest()),
        counters_(mock_query_buffer_underflow_, mock_record_name_overflow_, query_parsing_failure_,
                  queries_with_additional_rrs_, queries_with_ans_or_authority_rrs_) {
    setupResponseParser();
  }

  void setupResponseParser() { histogram_.unit_ = Stats::Histogram::Unit::Milliseconds; }

  void TearDown() override {
    cleanUpXdsConnection();
    if (test_server_) {
      test_server_.reset();
    }
    fake_upstreams_.clear();
  }

  static std::string configToUse() {
    return fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    load_assignment:
      cluster_name: cluster_0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: {}
                port_value: 0
    )EOF",
                       Platform::null_device_path,
                       Network::Test::getLoopbackAddressString(GetParam()));
  }

  Network::Address::InstanceConstSharedPtr getListenerBindAddressAndPortNoThrow() {
    auto addr = Network::Utility::parseInternetAddressAndPortNoThrow(
        fmt::format("{}:{}", Envoy::Network::Test::getLoopbackAddressUrlString(version_), 0),
        false);

    ASSERT(addr != nullptr);

    addr = Network::Test::findOrCheckFreePort(addr, Network::Socket::Type::Datagram);
    ASSERT(addr != nullptr && addr->ip() != nullptr);

    return addr;
  }

  envoy::config::listener::v3::Listener
  getListener0(Network::Address::InstanceConstSharedPtr& addr) {
    auto config = fmt::format(R"EOF(
name: listener_0
address:
  socket_address:
    address: {}
    port_value: 0
    protocol: udp
listener_filters:
  name: "envoy.filters.udp.dns_filter"
  typed_config:
    '@type': 'type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.DnsFilterConfig'
    stat_prefix: "my_prefix"
    client_config:
      resolver_timeout: 1s
      typed_dns_resolver_config:
        name: envoy.network.dns_resolver.getaddrinfo
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig
          num_retries:
            value: 1
      max_pending_lookups: 256
    server_config:
      inline_dns_table:
        external_retry_count: 0
        virtual_domains:
        - name: "im_just_here_so_coverage_doesnt_fail.foo1.com"
          endpoint:
            cluster_name: "cluster_0"
        - name: "www.foo1.com"
          endpoint:
            address_list:
              address:
              - 10.0.0.1
              - 10.0.0.2
              - 10.0.0.3
              - 10.0.0.4
        - name: "cluster.foo1.com"
          endpoint:
            cluster_name: "cluster_0"
        - name: "web.foo1.com"
          endpoint:
            service_list:
              services:
              - service_name: "http"
                protocol: {{ name: "tcp" }}
                ttl: 43200s
                targets:
                - cluster_name: "cluster_0"
                  weight: 10
                  priority: 40
                  port: 80
              - service_name: "https"
                protocol: {{ name: "tcp" }}
                ttl: 43200s
                targets:
                - cluster_name: "cluster_0"
                  weight: 20
                  priority: 10
        - name: "*.foo1.com"
          endpoint:
            address_list:
              address:
              - 10.10.0.1
              - 10.10.0.2
              - 10.10.0.3
)EOF",
                              addr->ip()->addressAsString(), addr->ip()->addressAsString(),
                              addr->ip()->port());
    return TestUtility::parseYaml<envoy::config::listener::v3::Listener>(config);
  }

  envoy::config::listener::v3::Listener
  getListener1(Network::Address::InstanceConstSharedPtr& addr) {
    auto config = fmt::format(R"EOF(
name: listener_1
address:
  socket_address:
    address: {}
    port_value: {}
    protocol: udp
listener_filters:
  name: "envoy.filters.udp.dns_filter"
  typed_config:
    '@type': 'type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.DnsFilterConfig'
    stat_prefix: "external_resolver"
    server_config:
      inline_dns_table:
        external_retry_count: 0
        virtual_domains:
        - name: "www.google.com"
          endpoint:
            address_list:
              address:
              - 42.42.42.42
              - 2607:42:42::42:42
)EOF",
                              addr->ip()->addressAsString(), addr->ip()->port());
    return TestUtility::parseYaml<envoy::config::listener::v3::Listener>(config);
  }

  envoy::config::core::v3::TypedExtensionConfig dnsTable(absl::string_view address,
                                                         absl::string_view resolver_address = {}) {
    const auto* resolver = fake_upstreams_[0]->localAddress()->ip();
    if (resolver_address.empty()) {
      resolver_address = resolver->addressAsString();
    }
    return TestUtility::parseYaml<envoy::config::core::v3::TypedExtensionConfig>(
        fmt::format(R"EOF(
name: dns_table
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.DnsFilterConfig
  stat_prefix: discovered_zones
  client_config:
    resolver_timeout: 60s
    max_pending_lookups: 10
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.cares
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
        resolvers:
        - socket_address: {{ address: "{}", port_value: {} }}
        dns_resolver_options: {{ no_default_search_domain: true }}
        query_timeout_seconds: 60
        query_tries: 1
  server_config:
    inline_dns_table:
      virtual_domains:
      - name: service.example
        endpoint:
          address_list:
            address: ["{}"]
)EOF",
                    resolver_address, resolver->port(), address));
  }

  void setup(uint32_t upstream_count) {
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());
    // Adding bootstrap default DNS resolver config.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* typed_dns_resolver_config = bootstrap.mutable_typed_dns_resolver_config();
      typed_dns_resolver_config->set_name("envoy.network.dns_resolver.getaddrinfo");
      envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
          config;
      config.mutable_num_retries()->set_value(1);
      std::ignore = typed_dns_resolver_config->mutable_typed_config()->PackFrom(config);
    });
    if (upstream_count > 1) {
      setDeterministicValue();
      setUpstreamCount(upstream_count);
      config_helper_.addConfigModifier(
          [upstream_count](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
            for (uint32_t i = 1; i < upstream_count; i++) {
              bootstrap.mutable_static_resources()
                  ->mutable_clusters(0)
                  ->mutable_load_assignment()
                  ->mutable_endpoints(0)
                  ->add_lb_endpoints()
                  ->mutable_endpoint()
                  ->MergeFrom(ConfigHelper::buildEndpoint(
                      Network::Test::getLoopbackAddressString(GetParam())));
            }
          });
    }

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto addr_port = getListenerBindAddressAndPortNoThrow();
      auto listener_0 = getListener0(addr_port);
      auto* listener0 = bootstrap.mutable_static_resources()->add_listeners();
      listener0->MergeFrom(listener_0);
      // Remove client_config for cluster lookup test cases.
      if (cluster_lookup_test_) {
        auto* listener_filter = listener0->mutable_listener_filters(0);
        envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig dns_filter_config;
        std::ignore = listener_filter->typed_config().UnpackTo(&dns_filter_config);

        dns_filter_config.clear_client_config();
        std::ignore = listener_filter->mutable_typed_config()->PackFrom(dns_filter_config);
      }
      auto listener_1 = getListener1(addr_port);
      if (create_xds_upstream_) {
        auto* filter = listener_1.mutable_listener_filters(0);
        filter->set_name("dns_table");
        auto* discovery = filter->mutable_config_discovery();
        *discovery->mutable_default_config() = dnsTable("192.0.2.5").typed_config();
        discovery->add_type_urls(discovery->default_config().type_url());
        discovery->set_apply_default_config_without_warming(true);
        auto* source = discovery->mutable_config_source();
        source->set_resource_api_version(envoy::config::core::v3::V3);
        source->mutable_ads();
      }
      bootstrap.mutable_static_resources()->add_listeners()->MergeFrom(listener_1);
    });

    initialize();
  }

  // Whether the stats store derives tags with the explicit-tags logic rather than the legacy
  // tag-extraction rules. Both modes must produce identical stat names, tag-extracted names and
  // tags, so the two IP versions run the server in different modes.
  bool explicitTags() const { return version_ == Network::Address::IpVersion::v6; }

  void initialize() override {
    // The mode is read during server initialization, before the runtime loader exists, so it has
    // to be set directly rather than with addRuntimeOverride().
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.enable_stats_explicit_tags",
                                  explicitTags());
    BaseIntegrationTest::initialize();
    auto* store = dynamic_cast<Stats::ThreadLocalStoreImpl*>(&test_server_->statStore());
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->useExplicitTags(), explicitTags());
  }

  void requestResponseWithListenerAddress(const Network::Address::Instance& listener_address,
                                          const std::string& data_to_send,
                                          Network::UdpRecvData& response_datagram) {
    Network::Test::UdpSyncPeer client(version_);
    client.write(data_to_send, listener_address);
    client.recv(response_datagram);
  }

  void dnsLookupTest(Network::Address::IpVersion ip_version, const std::string listener,
                     uint16_t rec_type) {
    mock_os_sys_calls_.setIpVersion(ip_version);
    setup(0);
    const uint32_t port = lookupPort(listener);
    const auto listener_address = *Network::Utility::resolveUrl(
        fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

    Network::UdpRecvData response;
    std::string query = Utils::buildQueryForDomain("www.google.com", rec_type, DNS_RECORD_CLASS_IN);
    requestResponseWithListenerAddress(*listener_address, query, response);

    response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
    EXPECT_TRUE(response_ctx_->parse_status_);

    EXPECT_EQ(1, response_ctx_->answers_.size());
    EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  }

  Api::ApiPtr api_;
  NiceMock<Stats::MockHistogram> histogram_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Stats::MockCounter> mock_query_buffer_underflow_;
  NiceMock<Stats::MockCounter> mock_record_name_overflow_;
  NiceMock<Stats::MockCounter> query_parsing_failure_;
  NiceMock<Stats::MockCounter> queries_with_additional_rrs_;
  NiceMock<Stats::MockCounter> queries_with_ans_or_authority_rrs_;
  DnsParserCounters counters_;
  DnsQueryContextPtr response_ctx_;
  OsSysCallsWithMockedDns mock_os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&mock_os_sys_calls_};
  bool cluster_lookup_test_ = false;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DnsFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsFilterIntegrationTest, ExternalLookupTest) {
  // Sending request to listener_0 triggers external DNS lookup.
  dnsLookupTest(Network::Address::IpVersion::v4, "listener_0", DNS_RECORD_TYPE_A);
}

TEST_P(DnsFilterIntegrationTest, InternalLookupTest) {
  // Sending request to listener_1 triggers internal DNS lookup.
  dnsLookupTest(Network::Address::IpVersion::v4, "listener_1", DNS_RECORD_TYPE_A);
}

TEST_P(DnsFilterIntegrationTest, EcdsUpdatesVirtualDomainOnActiveListener) {
  create_xds_upstream_ = true;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
    cluster->CopyFrom(bootstrap.static_resources().clusters(0));
    cluster->set_name("ads_cluster");
    cluster->mutable_load_assignment()->set_cluster_name("ads_cluster");
    ConfigHelper::setHttp2(*cluster);
    auto* ads = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    ads->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
    ads->set_transport_api_version(envoy::config::core::v3::V3);
    ads->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("ads_cluster");
  });
  setup(2);
  createXdsConnection();
  ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
  xds_stream_->startGrpcStream();
  const std::string type_url = "type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig";
  ASSERT_TRUE(compareDeltaDiscoveryRequest(type_url, {"dns_table"}, {}));
  const auto listener_address = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(version_), lookupPort("listener_1"));
  auto& resolver = *fake_upstreams_[0];
  auto& client = *fake_upstreams_[1];
  auto expectAnswer = [&](uint16_t id, const std::string& expected) {
    client.sendUdpDatagram(
        Utils::buildQueryForDomain("service.example", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN, id),
        listener_address);
    Network::UdpRecvData response;
    ASSERT_TRUE(client.waitForUdpDatagram(response));
    auto context = ResponseValidator::createResponseContext(response, counters_);
    ASSERT_TRUE(context->parse_status_);
    EXPECT_EQ(id, context->id_);
    ASSERT_EQ(1, context->answers_.size());
    Utils::verifyAddress({expected}, context->answers_.begin()->second);
  };
  constexpr char reload[] =
      "extension_config_discovery.udp_listener_filter.dns_table.config_reload";
  using Extension = envoy::config::core::v3::TypedExtensionConfig;
  auto expectAck = [&]() {
    ASSERT_TRUE(compareDeltaDiscoveryRequest(type_url, {}, {}, Grpc::Status::Ok, "", false));
  };
  expectAnswer(10, "192.0.2.5");
  sendDeltaDiscoveryResponse<Extension>(type_url, {dnsTable("192.0.2.10")}, {}, "1");
  expectAck();
  test_server_->waitForCounter(reload, testing::Eq(1));
  expectAnswer(1, "192.0.2.10");

  sendDeltaDiscoveryResponse<Extension>(type_url, {dnsTable("invalid-address")}, {}, "2");
  ASSERT_TRUE(compareDeltaDiscoveryRequest(type_url, {}, {}, Grpc::Status::Internal,
                                           "malformed IP address", false));
  test_server_->waitForCounter(
      "extension_config_discovery.udp_listener_filter.dns_table.config_fail", testing::Eq(1));
  expectAnswer(2, "192.0.2.10");

  // The virtual domain is valid, but a c-ares server must resolve to an IP address.
  sendDeltaDiscoveryResponse<Extension>(
      type_url, {dnsTable("192.0.2.99", "invalid-resolver.example")}, {}, "2b");
  ASSERT_TRUE(compareDeltaDiscoveryRequest(type_url, {}, {}, Grpc::Status::Internal,
                                           "malformed IP address: invalid-resolver.example",
                                           false));
  test_server_->waitForCounter(
      "extension_config_discovery.udp_listener_filter.dns_table.config_fail", testing::Eq(2));
  expectAnswer(11, "192.0.2.10");

  // Hold an external answer until the filter that forwarded this transaction has been replaced.
  client.sendUdpDatagram(
      Utils::buildQueryForDomain("pending.example", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN, 3),
      listener_address);
  Network::UdpRecvData forwarded;
  ASSERT_TRUE(resolver.waitForUdpDatagram(forwarded));
  DnsMessageParser parser(true, dispatcher_->timeSource(), 0, random_, histogram_);
  auto pending = parser.createQueryContext(forwarded, counters_);
  ASSERT_TRUE(pending->parse_status_);
  ASSERT_EQ(1, pending->queries_.size());
  EXPECT_THAT(pending->queries_.front()->name_, testing::StrCaseEq("pending.example"));
  ASSERT_TRUE(
      parser.storeDnsAnswerRecord(pending, *pending->queries_.front(), std::chrono::seconds(60),
                                  Network::Utility::parseInternetAddressNoThrow("192.0.2.30")));
  Buffer::OwnedImpl late_answer;
  parser.buildResponseBuffer(pending, late_answer);

  const uint64_t reloads_before_replacement = test_server_->counter(reload)->value();
  sendDeltaDiscoveryResponse<Extension>(type_url, {dnsTable("192.0.2.20")}, {}, "3");
  expectAck();
  test_server_->waitForCounter(reload, testing::Eq(reloads_before_replacement + 1));
  resolver.sendUdpDatagram(late_answer.toString(), forwarded.addresses_.peer_);
  expectAnswer(4, "192.0.2.20");
  Network::UdpRecvData unexpected;
  EXPECT_FALSE(client.waitForUdpDatagram(unexpected, std::chrono::milliseconds(50)));
  sendDeltaDiscoveryResponse<Extension>(type_url, {}, {"dns_table"}, "4");
  expectAck();
  test_server_->waitForCounter(reload, testing::Eq(reloads_before_replacement + 2));
  expectAnswer(5, "192.0.2.5");
  sendDeltaDiscoveryResponse<Extension>(type_url, {dnsTable("192.0.2.40")}, {}, "5");
  expectAck();
  test_server_->waitForCounter(reload, testing::Eq(reloads_before_replacement + 3));
  expectAnswer(6, "192.0.2.40");
  EXPECT_EQ(7,
            test_server_->counter("dns_filter.discovered_zones.downstream_tx_responses")->value());
  EXPECT_EQ(2 * concurrency_,
            test_server_->counter("listener_manager.listener_create_success")->value());
}

TEST_P(DnsFilterIntegrationTest, ExternalLookupTestIPv6) {
  dnsLookupTest(Network::Address::IpVersion::v6, "listener_0", DNS_RECORD_TYPE_AAAA);
}

TEST_P(DnsFilterIntegrationTest, InternalLookupTestIPv6) {
  dnsLookupTest(Network::Address::IpVersion::v6, "listener_1", DNS_RECORD_TYPE_AAAA);
}

TEST_P(DnsFilterIntegrationTest, LocalLookupTest) {
  setup(0);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("www.foo1.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(4, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
}

TEST_P(DnsFilterIntegrationTest, ClusterLookupTest) {
  cluster_lookup_test_ = true;
  setup(2);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  uint16_t record_type;
  if (listener_address->ip()->ipv6()) {
    record_type = DNS_RECORD_TYPE_AAAA;
  } else {
    record_type = DNS_RECORD_TYPE_A;
  }

  Network::UdpRecvData response;
  std::string query = Utils::buildQueryForDomain("cluster_0", record_type, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(2, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
}

TEST_P(DnsFilterIntegrationTest, ClusterEndpointLookupTest) {
  setup(2);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  uint16_t record_type;
  if (listener_address->ip()->ipv6()) {
    record_type = DNS_RECORD_TYPE_AAAA;
  } else {
    record_type = DNS_RECORD_TYPE_A;
  }

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("cluster.foo1.com", record_type, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(2, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
}

TEST_P(DnsFilterIntegrationTest, ClusterEndpointWithPortServiceRecordLookupTest) {
  setup(2);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  const std::string service("_http._tcp.web.foo1.com");
  Network::UdpRecvData response;
  std::string query = Utils::buildQueryForDomain(service, DNS_RECORD_TYPE_SRV, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(2, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.second->type_, DNS_RECORD_TYPE_SRV);

    DnsSrvRecord* srv_rec = dynamic_cast<DnsSrvRecord*>(answer.second.get());

    EXPECT_EQ(service, srv_rec->name_);
    EXPECT_EQ(43200, srv_rec->ttl_.count());

    EXPECT_EQ(1, srv_rec->targets_.size());
    const auto& target = srv_rec->targets_.begin();

    EXPECT_EQ(10, target->second.weight);
    EXPECT_EQ(40, target->second.priority);
    EXPECT_EQ(80, target->second.port);
  }
}

TEST_P(DnsFilterIntegrationTest, ClusterEndpointWithoutPortServiceRecordLookupTest) {
  constexpr size_t endpoints = 2;
  setup(endpoints);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  const std::string service("_https._tcp.web.foo1.com");
  Network::UdpRecvData response;
  std::string query = Utils::buildQueryForDomain(service, DNS_RECORD_TYPE_SRV, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(endpoints, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  std::set<uint16_t> ports;
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.second->type_, DNS_RECORD_TYPE_SRV);

    DnsSrvRecord* srv_rec = dynamic_cast<DnsSrvRecord*>(answer.second.get());

    EXPECT_EQ(service, srv_rec->name_);
    EXPECT_EQ(43200, srv_rec->ttl_.count());

    EXPECT_EQ(1, srv_rec->targets_.size());
    const auto& target = srv_rec->targets_.begin();

    EXPECT_EQ(20, target->second.weight);
    EXPECT_EQ(10, target->second.priority);

    // The port is unspecified and automatically assigned by the cluster
    EXPECT_NE(0, target->second.priority);
    ports.emplace(target->second.port);
  }

  EXPECT_EQ(endpoints, ports.size());
}

TEST_P(DnsFilterIntegrationTest, WildcardLookupTest) {
  setup(0);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("wild.foo1.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(3, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
}

// The stat prefix is an explicit tag of the stats in both stats modes.
TEST_P(DnsFilterIntegrationTest, StatsAreTagged) {
  setup(0);

  const auto queries = test_server_->counter("dns_filter.my_prefix.downstream_rx_queries");
  ASSERT_NE(queries.get(), nullptr);
  EXPECT_EQ("dns_filter.downstream_rx_queries", queries->tagExtractedName());
  EXPECT_THAT(queries->tags(), testing::UnorderedElementsAreArray(
                                   Stats::TagVector{{"envoy.dns_filter_prefix", "my_prefix"}}));

  const auto latency = test_server_->histogram("dns_filter.my_prefix.downstream_rx_query_latency");
  ASSERT_NE(latency.get(), nullptr);
  EXPECT_EQ("dns_filter.downstream_rx_query_latency", latency->tagExtractedName());
  EXPECT_THAT(latency->tags(), testing::UnorderedElementsAreArray(
                                   Stats::TagVector{{"envoy.dns_filter_prefix", "my_prefix"}}));
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
