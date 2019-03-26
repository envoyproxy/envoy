#include <chrono>
#include <memory>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/logical_dns_cluster.h"

#include "server/transport_socket_config_impl.h"

#include "source/extensions/clusters/redis/redis_cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"

//#include "extensions/clusters/redis/redis_cluster.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {
namespace {

class RedisDiscoverySessionTest : public testing::Test,
  public Extensions::NetworkFilters::Common::Redis::Client::ClientFactory {

public:

  RedisDiscoverySessionTest() : hosts_(new NiceMock<Upstream::MockClusterMockPrioritySet>()),
      event_logger_(new Upstream::MockHealthCheckEventLogger()) {}

  // ClientFactory
  Extensions::NetworkFilters::Common::Redis::Client::ClientPtr
  create(Upstream::HostConstSharedPtr, Event::Dispatcher&,
         const Extensions::NetworkFilters::Common::Redis::Client::Config&) override {
    return Extensions::NetworkFilters::Common::Redis::Client::ClientPtr{create_()};
  }

  MOCK_METHOD0(create_, Extensions::NetworkFilters::Common::Redis::Client::Client*());

  std::shared_ptr<Upstream::MockClusterMockPrioritySet> hosts_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Upstream::MockHealthCheckEventLogger* event_logger_{};
  Event::MockTimer* timeout_timer_{};
  Event::MockTimer* interval_timer_{};
  Extensions::NetworkFilters::Common::Redis::Client::MockClient* client_{};
  Extensions::NetworkFilters::Common::Redis::Client::MockPoolRequest pool_request_;
  Extensions::NetworkFilters::Common::Redis::Client::PoolCallbacks* pool_callbacks_{};
  std::shared_ptr<RedisCluster> cluster_;
};


TEST_F(RedisDiscoverySessionTest, HandleClusterSlotResponse) {
  
}


class RedisClusterTest : public testing::Test,
  public Extensions::NetworkFilters::Common::Redis::Client::ClientFactory {
public:
  // ClientFactory
  Extensions::NetworkFilters::Common::Redis::Client::ClientPtr
  create(Upstream::HostConstSharedPtr, Event::Dispatcher&,
         const Extensions::NetworkFilters::Common::Redis::Client::Config&) override {
    return Extensions::NetworkFilters::Common::Redis::Client::ClientPtr{create_()};
  }

  MOCK_METHOD0(create_, Extensions::NetworkFilters::Common::Redis::Client::Client*());

protected:
  RedisClusterTest() : api_(Api::createApiForTest(stats_store_)) {}

  void setupFromV2Yaml(const std::string& yaml) {
    resolve_timer_ = new Event::MockTimer(&dispatcher_);
    NiceMock<Upstream::MockClusterManager> cm;
    envoy::api::v2::Cluster cluster_config = Upstream::parseClusterFromV2Yaml(yaml);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_,
      singleton_manager_, tls_, *api_);
    auto config = MessageUtil::downcastAndValidate<const envoy::config::cluster::redis::RedisClusterConfig&>(cluster_config.cluster_type().typed_config());
    cluster_.reset(new RedisCluster(cluster_config, config, *this, cm, runtime_, dns_resolver_,
      factory_context, std::move(scope), false));
    cluster_->prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) -> void {
        membership_updated_.ready();
      });
  }

  void expectResolveDiscovery(Network::DnsLookupFamily dns_lookup_family,
                              const std::string &expected_address) {
    EXPECT_CALL(*dns_resolver_, resolve(expected_address, dns_lookup_family, _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        dns_callback_ = cb;
        return &active_dns_query_;
      }));
  }

  void expectRedisSessionCreated() {
    //interval_timer_ = new Event::MockTimer(&dispatcher_);
    //timeout_timer_ = new Event::MockTimer(&dispatcher_);
  }

  void expectRedisClientCreate() {
    client_ = new Extensions::NetworkFilters::Common::Redis::Client::MockClient();
    EXPECT_CALL(*this, create_()).WillOnce(Return(client_));
    EXPECT_CALL(*client_, addConnectionCallbacks(_));
  }

  void expectClusterSlotRequestCreate() {
    RedisCluster::ClusterSlotsRequest request;
    EXPECT_CALL(*client_, makeRequest(Ref(request), _))
      .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    EXPECT_CALL(*timeout_timer_, enableTimer(_));
  }

  void expectClusterSlotResponse() {
    std::vector<NetworkFilters::Common::Redis::RespValue> master_1(2);
    master_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    master_1[0].asString() = "127.0.0.1";
    master_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    master_1[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> slave_1(2);
    slave_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    slave_1[0].asString() = "127.0.0.2";
    slave_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slave_1[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_1(4);
    slot_1[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[0].asInteger() = 0;
    slot_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[1].asInteger() = 16383;
    slot_1[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[2].asArray().swap(master_1);
    slot_1[3].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[3].asArray().swap(slave_1);

    std::vector<NetworkFilters::Common::Redis::RespValue> slots(1);
    slots[0].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[0].asArray().swap(slot_1);

    NetworkFilters::Common::Redis::RespValuePtr response(
      new NetworkFilters::Common::Redis::RespValue());
    response->type(NetworkFilters::Common::Redis::RespType::Array);
    response->asArray().swap(slots);

    pool_callbacks_->onResponse(std::move(response));
  }

  void testBasicSetup(const std::string& config, const std::string& expected_discovery_address,
                      const uint32_t expected_port) {

    setupFromV2Yaml(config);

    expectResolveDiscovery(Network::DnsLookupFamily::V4Only, expected_discovery_address);
    cluster_->initialize([&]() -> void { initialized_.ready(); });

    EXPECT_CALL(membership_updated_, ready());
    EXPECT_CALL(initialized_, ready());
    EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(4000)));
    dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

    expectRedisSessionCreated();
    expectRedisClientCreate();
    expectClusterSlotRequestCreate();
    expectClusterSlotResponse();

    EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
    EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
    EXPECT_EQ(1UL,
              cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
    EXPECT_EQ(
      1UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
    EXPECT_EQ(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0],
              cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts()[0]);
    Upstream::HostSharedPtr logical_host = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0];

    EXPECT_EQ(fmt::format("127.0.0.1:{}", expected_port),
              logical_host->healthCheckAddress()->asString());
//
//    EXPECT_CALL(dispatcher_,
//                createClientConnection_(
//                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443")), _, _, _))
//      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
//    logical_host->createConnection(dispatcher_, nullptr, nullptr);
//    logical_host->outlierDetector().putHttpResponseCode(200);
//
//    expectResolveDiscovery(Network::DnsLookupFamily::V4Only, expected_discovery_address);
//    resolve_timer_->callback_();

    // Should not cause any changes.
//    EXPECT_CALL(*resolve_timer_, enableTimer(_));
//    dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2", "127.0.0.3"}));
//
//    EXPECT_NE("0.0.0.0:0", logical_host->healthCheckAddress()->asString());
//    EXPECT_EQ(fmt::format("127.0.0.1:{}", expected_port),
//              logical_host->healthCheckAddress()->asString());
//
//    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
//    EXPECT_CALL(dispatcher_,
//                createClientConnection_(
//                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443")), _, _, _))
//      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
//    Upstream::Host::CreateConnectionData data = logical_host->createConnection(dispatcher_, nullptr, nullptr);
//    EXPECT_FALSE(data.host_description_->canary());
//    EXPECT_EQ(&cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->cluster(),
//              &data.host_description_->cluster());
//    EXPECT_EQ(&cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->stats(),
//              &data.host_description_->stats());
//    EXPECT_EQ("127.0.0.1:443", data.host_description_->address()->asString());
//    EXPECT_EQ("", data.host_description_->locality().region());
//    EXPECT_EQ("", data.host_description_->locality().zone());
//    EXPECT_EQ("", data.host_description_->locality().sub_zone());
//    EXPECT_EQ("foo.bar.com", data.host_description_->hostname());
//    EXPECT_TRUE(TestUtility::protoEqual(envoy::api::v2::core::Metadata::default_instance(),
//                                        *data.host_description_->metadata()));
//    data.host_description_->outlierDetector().putHttpResponseCode(200);
//    data.host_description_->healthChecker().setUnhealthy();
//
//    expectResolveDiscovery(Network::DnsLookupFamily::V4Only, expected_discovery_address);
//    resolve_timer_->callback_();

    // Should cause a change.
//    EXPECT_CALL(*resolve_timer_, enableTimer(_));
//    dns_callback_(TestUtility::makeDnsResponse({"127.0.0.3", "127.0.0.1", "127.0.0.2"}));
//
//    EXPECT_NE("0.0.0.0:0", logical_host->healthCheckAddress()->asString());
//    EXPECT_EQ(fmt::format("127.0.0.3:{}", expected_port),
//              logical_host->healthCheckAddress()->asString());
//
//    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
//    EXPECT_CALL(dispatcher_,
//                createClientConnection_(
//                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _, _))
//      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
//    logical_host->createConnection(dispatcher_, nullptr, nullptr);
//
//    expectResolveDiscovery(Network::DnsLookupFamily::V4Only, expected_discovery_address);
//    resolve_timer_->callback_();
//
//    // Empty should not cause any change.
//    EXPECT_CALL(*resolve_timer_, enableTimer(_));
//    dns_callback_({});
//
//    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
//    EXPECT_CALL(dispatcher_,
//                createClientConnection_(
//                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _, _))
//      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
//    logical_host->createConnection(dispatcher_, nullptr, nullptr);
//
//    // Make sure we cancel.
//    EXPECT_CALL(active_dns_query_, cancel());
//    expectResolveDiscovery(Network::DnsLookupFamily::V4Only, expected_discovery_address);
//    resolve_timer_->callback_();
//
//    tls_.shutdownThread();
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
    new NiceMock<Network::MockDnsResolver>};
  Network::MockActiveDnsQuery active_dns_query_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Network::DnsResolver::ResolveCb dns_callback_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::MockTimer* resolve_timer_;
  std::shared_ptr<RedisCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest().currentThreadId()};
  Api::ApiPtr api_;
  std::shared_ptr<Upstream::MockClusterMockPrioritySet> hosts_;
  Upstream::MockHealthCheckEventLogger* event_logger_{};
  Event::MockTimer* timeout_timer_{};
  Event::MockTimer* interval_timer_{};
  Extensions::NetworkFilters::Common::Redis::Client::MockClient* client_{};
  Extensions::NetworkFilters::Common::Redis::Client::MockPoolRequest pool_request_;
  Extensions::NetworkFilters::Common::Redis::Client::PoolCallbacks* pool_callbacks_{};
};

typedef std::tuple<std::string, Network::DnsLookupFamily, std::list<std::string>>
  RedisConfigTuple;
std::vector<RedisConfigTuple> generateRedisParams() {
  std::vector<RedisConfigTuple> dns_config;
  {
    std::string family_json("");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "v4_only",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "v6_only",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V6Only);
    std::list<std::string> dns_response{"::1", "::2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "auto",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"::1"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  return dns_config;
}

class RedisParamTest : public RedisClusterTest,
                            public testing::WithParamInterface<RedisConfigTuple> {};

INSTANTIATE_TEST_SUITE_P(DnsParam, RedisParamTest,
  testing::ValuesIn(generateRedisParams()));

TEST_F(RedisClusterTest, Basic) {
  const std::string basic_yaml_hosts = R"EOF(
  name: name
  cluster_type: envoy.clusters.redis
  dns_lookup_family: V4_ONLY
  hosts:
  - socket_address:
      address: foo.bar.com
      port_value: 22120
  cluster_type:
    name: envoy.clusters.redis
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";

  testBasicSetup(basic_yaml_hosts, "foo.bar.com", 22120);
}

//
//class RedisHealthCheckerTest
//  : public testing::Test,
//    public Extensions::NetworkFilters::Common::Redis::Client::ClientFactory {
//public:
//  RedisHealthCheckerTest()
//    : cluster_(new NiceMock<Upstream::MockClusterMockPrioritySet>()),
//      event_logger_(new Upstream::MockHealthCheckEventLogger()) {}
//
//  void setup() {
//    const std::string yaml = R"EOF(
//    timeout: 1s
//    interval: 1s
//    no_traffic_interval: 5s
//    interval_jitter: 1s
//    unhealthy_threshold: 1
//    healthy_threshold: 1
//    custom_health_check:
//      name: envoy.health_checkers.redis
//      config:
//    )EOF";
//
//    const auto& health_check_config = Upstream::parseHealthCheckFromV2Yaml(yaml);
//    const auto& redis_config = getRedisHealthCheckConfig(health_check_config);
//
//    health_checker_.reset(
//      new RedisHealthChecker(*cluster_, health_check_config, redis_config, dispatcher_, runtime_,
//                             random_, Upstream::HealthCheckEventLoggerPtr(event_logger_), *this));
//  }
//
//  void setupAlwaysLogHealthCheckFailures() {
//    const std::string yaml = R"EOF(
//    timeout: 1s
//    interval: 1s
//    no_traffic_interval: 5s
//    interval_jitter: 1s
//    unhealthy_threshold: 1
//    healthy_threshold: 1
//    always_log_health_check_failures: true
//    custom_health_check:
//      name: envoy.health_checkers.redis
//      config:
//    )EOF";
//
//    const auto& health_check_config = Upstream::parseHealthCheckFromV2Yaml(yaml);
//    const auto& redis_config = getRedisHealthCheckConfig(health_check_config);
//
//    health_checker_.reset(
//      new RedisHealthChecker(*cluster_, health_check_config, redis_config, dispatcher_, runtime_,
//                             random_, Upstream::HealthCheckEventLoggerPtr(event_logger_), *this));
//  }
//
//  void setupExistsHealthcheck() {
//    const std::string yaml = R"EOF(
//    timeout: 1s
//    interval: 1s
//    no_traffic_interval: 5s
//    interval_jitter: 1s
//    unhealthy_threshold: 1
//    healthy_threshold: 1
//    custom_health_check:
//      name: envoy.health_checkers.redis
//      config:
//        key: foo
//    )EOF";
//
//    const auto& health_check_config = Upstream::parseHealthCheckFromV2Yaml(yaml);
//    const auto& redis_config = getRedisHealthCheckConfig(health_check_config);
//
//    health_checker_.reset(
//      new RedisHealthChecker(*cluster_, health_check_config, redis_config, dispatcher_, runtime_,
//                             random_, Upstream::HealthCheckEventLoggerPtr(event_logger_), *this));
//  }
//
//  void setupDontReuseConnection() {
//    const std::string yaml = R"EOF(
//    timeout: 1s
//    interval: 1s
//    no_traffic_interval: 5s
//    interval_jitter: 1s
//    unhealthy_threshold: 1
//    healthy_threshold: 1
//    reuse_connection: false
//    custom_health_check:
//      name: envoy.health_checkers.redis
//      config:
//    )EOF";
//
//    const auto& health_check_config = Upstream::parseHealthCheckFromV2Yaml(yaml);
//    const auto& redis_config = getRedisHealthCheckConfig(health_check_config);
//
//    health_checker_.reset(
//      new RedisHealthChecker(*cluster_, health_check_config, redis_config, dispatcher_, runtime_,
//                             random_, Upstream::HealthCheckEventLoggerPtr(event_logger_), *this));
//  }
//
//  Extensions::NetworkFilters::Common::Redis::Client::ClientPtr
//  create(Upstream::HostConstSharedPtr, Event::Dispatcher&,
//         const Extensions::NetworkFilters::Common::Redis::Client::Config&) override {
//    return Extensions::NetworkFilters::Common::Redis::Client::ClientPtr{create_()};
//  }
//
//  MOCK_METHOD0(create_, Extensions::NetworkFilters::Common::Redis::Client::Client*());
//
//  void expectSessionCreate() {
//    interval_timer_ = new Event::MockTimer(&dispatcher_);
//    timeout_timer_ = new Event::MockTimer(&dispatcher_);
//  }
//
//  void expectClientCreate() {
//    client_ = new Extensions::NetworkFilters::Common::Redis::Client::MockClient();
//    EXPECT_CALL(*this, create_()).WillOnce(Return(client_));
//    EXPECT_CALL(*client_, addConnectionCallbacks(_));
//  }
//
//  void expectExistsRequestCreate() {
//    EXPECT_CALL(*client_, makeRequest(Ref(RedisHealthChecker::existsHealthCheckRequest("")), _))
//      .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
//    EXPECT_CALL(*timeout_timer_, enableTimer(_));
//  }
//
//  void expectPingRequestCreate() {
//    EXPECT_CALL(*client_, makeRequest(Ref(RedisHealthChecker::pingHealthCheckRequest()), _))
//      .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
//    EXPECT_CALL(*timeout_timer_, enableTimer(_));
//  }
//
//  std::shared_ptr<Upstream::MockClusterMockPrioritySet> cluster_;
//  NiceMock<Event::MockDispatcher> dispatcher_;
//  NiceMock<Runtime::MockLoader> runtime_;
//  NiceMock<Runtime::MockRandomGenerator> random_;
//  Upstream::MockHealthCheckEventLogger* event_logger_{};
//  Event::MockTimer* timeout_timer_{};
//  Event::MockTimer* interval_timer_{};
//  Extensions::NetworkFilters::Common::Redis::Client::MockClient* client_{};
//  Extensions::NetworkFilters::Common::Redis::Client::MockPoolRequest pool_request_;
//  Extensions::NetworkFilters::Common::Redis::Client::PoolCallbacks* pool_callbacks_{};
//  std::shared_ptr<RedisHealthChecker> health_checker_;
//};
//
//TEST_F(RedisHealthCheckerTest, PingAndVariousFailures) {
//InSequence s;
//setup();
//
//cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
//  Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
//
//expectSessionCreate();
//expectClientCreate();
//expectPingRequestCreate();
//health_checker_->start();
//
//client_->runHighWatermarkCallbacks();
//client_->runLowWatermarkCallbacks();
//
//// Success
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//NetworkFilters::Common::Redis::RespValuePtr response(
//  new NetworkFilters::Common::Redis::RespValue());
//response->type(NetworkFilters::Common::Redis::RespType::SimpleString);
//response->asString() = "PONG";
//pool_callbacks_->onResponse(std::move(response));
//
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Failure
//EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//response = std::make_unique<NetworkFilters::Common::Redis::RespValue>();
//pool_callbacks_->onResponse(std::move(response));
//
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Redis failure via disconnect
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//pool_callbacks_->onFailure();
//client_->raiseEvent(Network::ConnectionEvent::RemoteClose);
//
//expectClientCreate();
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Timeout
//EXPECT_CALL(pool_request_, cancel());
//EXPECT_CALL(*client_, close());
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//timeout_timer_->callback_();
//
//expectClientCreate();
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Shutdown with active request.
//EXPECT_CALL(pool_request_, cancel());
//EXPECT_CALL(*client_, close());
//
//EXPECT_EQ(5UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
//EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
//EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
//EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
//}
//
//TEST_F(RedisHealthCheckerTest, FailuresLogging) {
//InSequence s;
//setupAlwaysLogHealthCheckFailures();
//
//cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
//  Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
//
//expectSessionCreate();
//expectClientCreate();
//expectPingRequestCreate();
//health_checker_->start();
//
//client_->runHighWatermarkCallbacks();
//client_->runLowWatermarkCallbacks();
//
//// Success
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//NetworkFilters::Common::Redis::RespValuePtr response(
//  new NetworkFilters::Common::Redis::RespValue());
//response->type(NetworkFilters::Common::Redis::RespType::SimpleString);
//response->asString() = "PONG";
//pool_callbacks_->onResponse(std::move(response));
//
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Failure
//EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
//EXPECT_CALL(*event_logger_, logUnhealthy(_, _, _, false));
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//response = std::make_unique<NetworkFilters::Common::Redis::RespValue>();
//pool_callbacks_->onResponse(std::move(response));
//
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Fail again
//EXPECT_CALL(*event_logger_, logUnhealthy(_, _, _, false));
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//response = std::make_unique<NetworkFilters::Common::Redis::RespValue>();
//pool_callbacks_->onResponse(std::move(response));
//
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Shutdown with active request.
//EXPECT_CALL(pool_request_, cancel());
//EXPECT_CALL(*client_, close());
//
//EXPECT_EQ(4UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
//EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
//EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
//EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
//}
//
//TEST_F(RedisHealthCheckerTest, LogInitialFailure) {
//InSequence s;
//setup();
//
//cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
//  Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
//
//expectSessionCreate();
//expectClientCreate();
//expectPingRequestCreate();
//health_checker_->start();
//
//client_->runHighWatermarkCallbacks();
//client_->runLowWatermarkCallbacks();
//
//// Redis failure via disconnect
//EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
//EXPECT_CALL(*event_logger_, logUnhealthy(_, _, _, true));
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//pool_callbacks_->onFailure();
//client_->raiseEvent(Network::ConnectionEvent::RemoteClose);
//
//expectClientCreate();
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Success
//EXPECT_CALL(*event_logger_, logAddHealthy(_, _, false));
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//NetworkFilters::Common::Redis::RespValuePtr response(
//  new NetworkFilters::Common::Redis::RespValue());
//response->type(NetworkFilters::Common::Redis::RespType::SimpleString);
//response->asString() = "PONG";
//pool_callbacks_->onResponse(std::move(response));
//
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Shutdown with active request.
//EXPECT_CALL(pool_request_, cancel());
//EXPECT_CALL(*client_, close());
//
//EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
//EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
//EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
//EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
//}
//
//TEST_F(RedisHealthCheckerTest, Exists) {
//InSequence s;
//setupExistsHealthcheck();
//
//cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
//  Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
//
//expectSessionCreate();
//expectClientCreate();
//expectExistsRequestCreate();
//health_checker_->start();
//
//client_->runHighWatermarkCallbacks();
//client_->runLowWatermarkCallbacks();
//
//// Success
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//NetworkFilters::Common::Redis::RespValuePtr response(
//  new NetworkFilters::Common::Redis::RespValue());
//response->type(NetworkFilters::Common::Redis::RespType::Integer);
//response->asInteger() = 0;
//pool_callbacks_->onResponse(std::move(response));
//
//expectExistsRequestCreate();
//interval_timer_->callback_();
//
//// Failure, exists
//EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//response = std::make_unique<NetworkFilters::Common::Redis::RespValue>();
//response->type(NetworkFilters::Common::Redis::RespType::Integer);
//response->asInteger() = 1;
//pool_callbacks_->onResponse(std::move(response));
//
//expectExistsRequestCreate();
//interval_timer_->callback_();
//
//// Failure, no value
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//response = std::make_unique<NetworkFilters::Common::Redis::RespValue>();
//pool_callbacks_->onResponse(std::move(response));
//
//EXPECT_CALL(*client_, close());
//
//EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
//EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
//EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
//}
//
//// Tests that redis client will behave appropriately when reuse_connection is false.
//TEST_F(RedisHealthCheckerTest, NoConnectionReuse) {
//InSequence s;
//setupDontReuseConnection();
//
//cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
//  Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
//
//expectSessionCreate();
//expectClientCreate();
//expectPingRequestCreate();
//health_checker_->start();
//
//// The connection will close on success.
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//EXPECT_CALL(*client_, close());
//NetworkFilters::Common::Redis::RespValuePtr response(
//  new NetworkFilters::Common::Redis::RespValue());
//response->type(NetworkFilters::Common::Redis::RespType::SimpleString);
//response->asString() = "PONG";
//pool_callbacks_->onResponse(std::move(response));
//
//expectClientCreate();
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// The connection will close on failure.
//EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//EXPECT_CALL(*client_, close());
//response = std::make_unique<NetworkFilters::Common::Redis::RespValue>();
//pool_callbacks_->onResponse(std::move(response));
//
//expectClientCreate();
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Redis failure via disconnect, the connection was closed by the other end.
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//pool_callbacks_->onFailure();
//client_->raiseEvent(Network::ConnectionEvent::RemoteClose);
//
//expectClientCreate();
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Timeout, the connection will be closed.
//EXPECT_CALL(pool_request_, cancel());
//EXPECT_CALL(*client_, close());
//EXPECT_CALL(*timeout_timer_, disableTimer());
//EXPECT_CALL(*interval_timer_, enableTimer(_));
//timeout_timer_->callback_();
//
//expectClientCreate();
//expectPingRequestCreate();
//interval_timer_->callback_();
//
//// Shutdown with active request.
//EXPECT_CALL(pool_request_, cancel());
//EXPECT_CALL(*client_, close());
//
//// The metrics expected after all tests have run.
//EXPECT_EQ(5UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
//EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
//EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
//EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
//}

} // namespace
} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
