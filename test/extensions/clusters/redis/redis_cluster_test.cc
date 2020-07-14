#include <bitset>
#include <chrono>
#include <memory>
#include <vector>

#include "envoy/config/cluster/redis/redis_cluster.pb.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.validate.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/logical_dns_cluster.h"

#include "source/extensions/clusters/redis/redis_cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/clusters/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"

using testing::_;
using testing::ContainerEq;
using testing::Eq;
using testing::NiceMock;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

namespace {
const std::string BasicConfig = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
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
}

static const int ResponseFlagSize = 11;
static const int ResponseReplicaFlagSize = 4;
class RedisClusterTest : public testing::Test,
                         public Extensions::NetworkFilters::Common::Redis::Client::ClientFactory {
public:
  // ClientFactory
  Extensions::NetworkFilters::Common::Redis::Client::ClientPtr
  create(Upstream::HostConstSharedPtr host, Event::Dispatcher&,
         const Extensions::NetworkFilters::Common::Redis::Client::Config&,
         const Extensions::NetworkFilters::Common::Redis::RedisCommandStatsSharedPtr&,
         Stats::Scope&, const std::string&, const std::string&) override {
    EXPECT_EQ(22120, host->address()->ip()->port());
    return Extensions::NetworkFilters::Common::Redis::Client::ClientPtr{
        create_(host->address()->asString())};
  }

  MOCK_METHOD(Extensions::NetworkFilters::Common::Redis::Client::Client*, create_, (std::string));

protected:
  RedisClusterTest() : api_(Api::createApiForTest(stats_store_)) {}

  std::list<std::string> hostListToAddresses(const Upstream::HostVector& hosts) {
    std::list<std::string> addresses;
    for (const Upstream::HostSharedPtr& host : hosts) {
      addresses.push_back(host->address()->asString());
    }

    return addresses;
  }

  void setupFromV3Yaml(const std::string& yaml) {
    expectRedisSessionCreated();
    NiceMock<Upstream::MockClusterManager> cm;
    envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_,
        singleton_manager_, tls_, validation_visitor_, *api_);

    envoy::config::cluster::redis::RedisClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufWkt::Struct::default_instance(),
                                           ProtobufMessage::getStrictValidationVisitor(), config);
    cluster_callback_ = std::make_shared<NiceMock<MockClusterSlotUpdateCallBack>>();
    cluster_ = std::make_shared<RedisCluster>(
        cluster_config,
        TestUtility::downcastAndValidate<const envoy::config::cluster::redis::RedisClusterConfig&>(
            config),
        *this, cm, runtime_, *api_, dns_resolver_, factory_context, std::move(scope), false,
        cluster_callback_);
    // This allows us to create expectation on cluster slot response without waiting for
    // makeRequest.
    pool_callbacks_ = &cluster_->redis_discovery_session_;
    cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) -> void {
          membership_updated_.ready();
        });
  }

  void setupFactoryFromV2Yaml(const std::string& yaml) {
    NiceMock<Upstream::MockClusterManager> cm;
    envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_,
        singleton_manager_, tls_, validation_visitor_, *api_);

    envoy::config::cluster::redis::RedisClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufWkt::Struct::default_instance(),
                                           validation_visitor_, config);

    NiceMock<AccessLog::MockAccessLogManager> log_manager;
    NiceMock<Upstream::Outlier::EventLoggerSharedPtr> outlier_event_logger;
    NiceMock<Envoy::Api::MockApi> api;
    Upstream::ClusterFactoryContextImpl cluster_factory_context(
        cm, stats_store_, tls_, std::move(dns_resolver_), ssl_context_manager_, runtime_, random_,
        dispatcher_, log_manager, local_info_, admin_, singleton_manager_,
        std::move(outlier_event_logger), false, validation_visitor_, api);

    RedisClusterFactory factory = RedisClusterFactory();
    factory.createClusterWithConfig(cluster_config, config, cluster_factory_context,
                                    factory_context, std::move(scope));
  }

  void expectResolveDiscovery(Network::DnsLookupFamily dns_lookup_family,
                              const std::string& expected_address,
                              const std::list<std::string>& resolved_addresses,
                              Network::DnsResolver::ResolutionStatus status =
                                  Network::DnsResolver::ResolutionStatus::Success) {
    EXPECT_CALL(*dns_resolver_, resolve(expected_address, dns_lookup_family, _))
        .WillOnce(Invoke([status, resolved_addresses](
                             const std::string&, Network::DnsLookupFamily,
                             Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          cb(status, TestUtility::makeDnsResponse(resolved_addresses));
          return nullptr;
        }));
  }

  void expectRedisSessionCreated() {
    resolve_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*resolve_timer_, disableTimer());
    ON_CALL(random_, random()).WillByDefault(Return(0));
  }

  void expectRedisResolve(bool create_client = false) {
    if (create_client) {
      client_ = new Extensions::NetworkFilters::Common::Redis::Client::MockClient();
      EXPECT_CALL(*this, create_(_)).WillOnce(Return(client_));
      EXPECT_CALL(*client_, addConnectionCallbacks(_));
      EXPECT_CALL(*client_, close());
    }
    EXPECT_CALL(*client_, makeRequest_(Ref(RedisCluster::ClusterSlotsRequest::instance_), _))
        .WillOnce(Return(&pool_request_));
  }

  void expectClusterSlotResponse(NetworkFilters::Common::Redis::RespValuePtr&& response) {
    EXPECT_CALL(*resolve_timer_, enableTimer(_, _));
    pool_callbacks_->onResponse(std::move(response));
  }

  void expectClusterSlotFailure() {
    EXPECT_CALL(*resolve_timer_, enableTimer(_, _));
    pool_callbacks_->onFailure();
  }

  NetworkFilters::Common::Redis::RespValuePtr singleSlotPrimaryReplica(const std::string& primary,
                                                                       const std::string& replica,
                                                                       int64_t port) const {
    std::vector<NetworkFilters::Common::Redis::RespValue> primary_1(2);
    primary_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    primary_1[0].asString() = primary;
    primary_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    primary_1[1].asInteger() = port;

    std::vector<NetworkFilters::Common::Redis::RespValue> replica_1(2);
    replica_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    replica_1[0].asString() = replica;
    replica_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    replica_1[1].asInteger() = port;

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_1(ResponseReplicaFlagSize);
    slot_1[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[0].asInteger() = 0;
    slot_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[1].asInteger() = 16383;
    slot_1[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[2].asArray().swap(primary_1);
    slot_1[3].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[3].asArray().swap(replica_1);

    std::vector<NetworkFilters::Common::Redis::RespValue> slots(1);
    slots[0].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[0].asArray().swap(slot_1);

    NetworkFilters::Common::Redis::RespValuePtr response(
        new NetworkFilters::Common::Redis::RespValue());
    response->type(NetworkFilters::Common::Redis::RespType::Array);
    response->asArray().swap(slots);
    return response;
  }

  NetworkFilters::Common::Redis::RespValuePtr twoSlotsPrimaries() const {
    std::vector<NetworkFilters::Common::Redis::RespValue> primary_1(2);
    primary_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    primary_1[0].asString() = "127.0.0.1";
    primary_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    primary_1[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> primary_2(2);
    primary_2[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    primary_2[0].asString() = "127.0.0.2";
    primary_2[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    primary_2[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_1(3);
    slot_1[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[0].asInteger() = 0;
    slot_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[1].asInteger() = 9999;
    slot_1[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[2].asArray().swap(primary_1);

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_2(3);
    slot_2[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_2[0].asInteger() = 10000;
    slot_2[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_2[1].asInteger() = 16383;
    slot_2[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_2[2].asArray().swap(primary_2);

    std::vector<NetworkFilters::Common::Redis::RespValue> slots(2);
    slots[0].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[0].asArray().swap(slot_1);
    slots[1].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[1].asArray().swap(slot_2);

    NetworkFilters::Common::Redis::RespValuePtr response(
        new NetworkFilters::Common::Redis::RespValue());
    response->type(NetworkFilters::Common::Redis::RespType::Array);
    response->asArray().swap(slots);
    return response;
  }

  NetworkFilters::Common::Redis::RespValuePtr twoSlotsPrimariesWithReplica() const {
    std::vector<NetworkFilters::Common::Redis::RespValue> primary_1(2);
    primary_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    primary_1[0].asString() = "127.0.0.1";
    primary_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    primary_1[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> primary_2(2);
    primary_2[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    primary_2[0].asString() = "127.0.0.2";
    primary_2[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    primary_2[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> replica_1(2);
    replica_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    replica_1[0].asString() = "127.0.0.3";
    replica_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    replica_1[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> replica_2(2);
    replica_2[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    replica_2[0].asString() = "127.0.0.4";
    replica_2[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    replica_2[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_1(ResponseReplicaFlagSize);
    slot_1[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[0].asInteger() = 0;
    slot_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[1].asInteger() = 9999;
    slot_1[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[2].asArray().swap(primary_1);
    slot_1[3].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[3].asArray().swap(replica_1);

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_2(ResponseReplicaFlagSize);
    slot_2[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_2[0].asInteger() = 10000;
    slot_2[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_2[1].asInteger() = 16383;
    slot_2[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_2[2].asArray().swap(primary_2);
    slot_2[3].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_2[3].asArray().swap(replica_2);

    std::vector<NetworkFilters::Common::Redis::RespValue> slots(2);
    slots[0].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[0].asArray().swap(slot_1);
    slots[1].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[1].asArray().swap(slot_2);

    NetworkFilters::Common::Redis::RespValuePtr response(
        new NetworkFilters::Common::Redis::RespValue());
    response->type(NetworkFilters::Common::Redis::RespType::Array);
    response->asArray().swap(slots);
    return response;
  }

  NetworkFilters::Common::Redis::RespValue
  createStringField(bool is_correct_type, const std::string& correct_value) const {
    NetworkFilters::Common::Redis::RespValue respValue;
    if (is_correct_type) {
      respValue.type(NetworkFilters::Common::Redis::RespType::BulkString);
      respValue.asString() = correct_value;
    } else {
      respValue.type(NetworkFilters::Common::Redis::RespType::Integer);
      respValue.asInteger() = ResponseFlagSize;
    }
    return respValue;
  }

  NetworkFilters::Common::Redis::RespValue createIntegerField(bool is_correct_type,
                                                              int64_t correct_value) const {
    NetworkFilters::Common::Redis::RespValue respValue;
    if (is_correct_type) {
      respValue.type(NetworkFilters::Common::Redis::RespType::Integer);
      respValue.asInteger() = correct_value;
    } else {
      respValue.type(NetworkFilters::Common::Redis::RespType::BulkString);
      respValue.asString() = "bad_value";
    }
    return respValue;
  }

  NetworkFilters::Common::Redis::RespValue
  createArrayField(bool is_correct_type,
                   std::vector<NetworkFilters::Common::Redis::RespValue>& correct_value) const {
    NetworkFilters::Common::Redis::RespValue respValue;
    if (is_correct_type) {
      respValue.type(NetworkFilters::Common::Redis::RespType::Array);
      respValue.asArray().swap(correct_value);
    } else {
      respValue.type(NetworkFilters::Common::Redis::RespType::BulkString);
      respValue.asString() = "bad value";
    }
    return respValue;
  }

  // Create a redis cluster slot response. If a bit is set in the bitset, then that part of
  // of the response is correct, otherwise it's incorrect.
  NetworkFilters::Common::Redis::RespValuePtr
  createResponse(std::bitset<ResponseFlagSize> flags,
                 std::bitset<ResponseReplicaFlagSize> replica_flags) const {
    int64_t idx(0);
    int64_t slots_type = idx++;
    int64_t slots_size = idx++;
    int64_t slot1_type = idx++;
    int64_t slot1_size = idx++;
    int64_t slot1_range_start_type = idx++;
    int64_t slot1_range_end_type = idx++;
    int64_t primary_type = idx++;
    int64_t primary_size = idx++;
    int64_t primary_ip_type = idx++;
    int64_t primary_ip_value = idx++;
    int64_t primary_port_type = idx++;
    idx = 0;
    int64_t replica_size = idx++;
    int64_t replica_ip_type = idx++;
    int64_t replica_ip_value = idx++;
    int64_t replica_port_type = idx++;

    std::vector<NetworkFilters::Common::Redis::RespValue> primary_1_array;
    if (flags.test(primary_size)) {
      // Ip field.
      if (flags.test(primary_ip_value)) {
        primary_1_array.push_back(createStringField(flags.test(primary_ip_type), "127.0.0.1"));
      } else {
        primary_1_array.push_back(createStringField(flags.test(primary_ip_type), "bad ip foo"));
      }
      // Port field.
      primary_1_array.push_back(createIntegerField(flags.test(primary_port_type), 22120));
    }

    std::vector<NetworkFilters::Common::Redis::RespValue> replica_1_array;
    if (replica_flags.any()) {
      // Ip field.
      if (replica_flags.test(replica_ip_value)) {
        replica_1_array.push_back(
            createStringField(replica_flags.test(replica_ip_type), "127.0.0.2"));
      } else {
        replica_1_array.push_back(
            createStringField(replica_flags.test(replica_ip_type), "bad ip bar"));
      }
      // Port field.
      replica_1_array.push_back(createIntegerField(replica_flags.test(replica_port_type), 22120));
    }

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_1_array;
    if (flags.test(slot1_size)) {
      slot_1_array.push_back(createIntegerField(flags.test(slot1_range_start_type), 0));
      slot_1_array.push_back(createIntegerField(flags.test(slot1_range_end_type), 16383));
      slot_1_array.push_back(createArrayField(flags.test(primary_type), primary_1_array));
      if (replica_flags.any()) {
        slot_1_array.push_back(createArrayField(replica_flags.test(replica_size), replica_1_array));
      }
    }

    std::vector<NetworkFilters::Common::Redis::RespValue> slots_array;
    if (flags.test(slots_size)) {
      slots_array.push_back(createArrayField(flags.test(slot1_type), slot_1_array));
    }

    NetworkFilters::Common::Redis::RespValuePtr response{
        new NetworkFilters::Common::Redis::RespValue()};
    if (flags.test(slots_type)) {
      response->type(NetworkFilters::Common::Redis::RespType::Array);
      response->asArray().swap(slots_array);
    } else {
      response->type(NetworkFilters::Common::Redis::RespType::BulkString);
      response->asString() = "Pong";
    }

    return response;
  }

  void
  expectHealthyHosts(const std::list<std::string, std::allocator<std::string>>& healthy_hosts) {
    EXPECT_THAT(healthy_hosts, ContainerEq(hostListToAddresses(
                                   cluster_->prioritySet().hostSetsPerPriority()[0]->hosts())));
    EXPECT_THAT(healthy_hosts,
                ContainerEq(hostListToAddresses(
                    cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts())));
    EXPECT_EQ(1UL,
              cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
    EXPECT_EQ(
        1UL,
        cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  }

  void testBasicSetup(const std::string& config, const std::string& expected_discovery_address) {
    setupFromV3Yaml(config);
    const std::list<std::string> resolved_addresses{"127.0.0.1", "127.0.0.2"};
    expectResolveDiscovery(Network::DnsLookupFamily::V4Only, expected_discovery_address,
                           resolved_addresses);
    expectRedisResolve(true);

    EXPECT_CALL(membership_updated_, ready());
    EXPECT_CALL(initialized_, ready());
    cluster_->initialize([&]() -> void { initialized_.ready(); });

    EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
    expectClusterSlotResponse(singleSlotPrimaryReplica("127.0.0.1", "127.0.0.2", 22120));
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));

    // Promote replica to primary
    expectRedisResolve();
    EXPECT_CALL(membership_updated_, ready());
    resolve_timer_->invokeCallback();
    EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
    expectClusterSlotResponse(twoSlotsPrimaries());
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));

    // No change.
    expectRedisResolve();
    resolve_timer_->invokeCallback();
    EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1).WillOnce(Return(false));
    expectClusterSlotResponse(twoSlotsPrimaries());
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));

    // Add replicas to primaries
    expectRedisResolve();
    EXPECT_CALL(membership_updated_, ready());
    resolve_timer_->invokeCallback();
    EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
    expectClusterSlotResponse(twoSlotsPrimariesWithReplica());
    expectHealthyHosts(std::list<std::string>(
        {"127.0.0.1:22120", "127.0.0.3:22120", "127.0.0.2:22120", "127.0.0.4:22120"}));

    // No change.
    expectRedisResolve();
    resolve_timer_->invokeCallback();
    EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1).WillOnce(Return(false));
    expectClusterSlotResponse(twoSlotsPrimariesWithReplica());
    expectHealthyHosts(std::list<std::string>(
        {"127.0.0.1:22120", "127.0.0.3:22120", "127.0.0.2:22120", "127.0.0.4:22120"}));

    // Remove 2nd shard.
    expectRedisResolve();
    EXPECT_CALL(membership_updated_, ready());
    resolve_timer_->invokeCallback();
    EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
    expectClusterSlotResponse(singleSlotPrimaryReplica("127.0.0.1", "127.0.0.2", 22120));
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));
  }

  void exerciseStubs() {
    EXPECT_CALL(dispatcher_, createTimer_(_));
    RedisCluster::RedisDiscoverySession discovery_session(*cluster_, *this);
    EXPECT_FALSE(discovery_session.enableHashtagging());
    EXPECT_EQ(discovery_session.bufferFlushTimeoutInMs(), std::chrono::milliseconds(0));
    EXPECT_EQ(discovery_session.maxUpstreamUnknownConnections(), 0);

    NetworkFilters::Common::Redis::RespValuePtr dummy_value{
        new NetworkFilters::Common::Redis::RespValue()};
    dummy_value->type(NetworkFilters::Common::Redis::RespType::Error);
    dummy_value->asString() = "dummy text";
    EXPECT_TRUE(discovery_session.onRedirection(std::move(dummy_value), "dummy ip", false));

    RedisCluster::RedisDiscoveryClient discovery_client(discovery_session);
    EXPECT_NO_THROW(discovery_client.onAboveWriteBufferHighWatermark());
    EXPECT_NO_THROW(discovery_client.onBelowWriteBufferLowWatermark());
  }

  void testDnsResolve(const char* const address, const int port) {
    RedisCluster::DnsDiscoveryResolveTarget resolver_target(*cluster_, address, port);
    EXPECT_CALL(*dns_resolver_, resolve(address, Network::DnsLookupFamily::V4Only, _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                             Network::DnsResolver::ResolveCb) -> Network::ActiveDnsQuery* {
          return &active_dns_query_;
        }));
    ;
    resolver_target.startResolveDns();

    EXPECT_CALL(active_dns_query_, cancel());
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::MockTimer* resolve_timer_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
  std::shared_ptr<Upstream::MockClusterMockPrioritySet> hosts_;
  Upstream::MockHealthCheckEventLogger* event_logger_{};
  Event::MockTimer* interval_timer_{};
  Extensions::NetworkFilters::Common::Redis::Client::MockClient* client_{};
  Extensions::NetworkFilters::Common::Redis::Client::MockPoolRequest pool_request_;
  Extensions::NetworkFilters::Common::Redis::Client::ClientCallbacks* pool_callbacks_{};
  std::shared_ptr<RedisCluster> cluster_;
  std::shared_ptr<NiceMock<MockClusterSlotUpdateCallBack>> cluster_callback_;
  Network::MockActiveDnsQuery active_dns_query_;
};

using RedisDnsConfigTuple = std::tuple<std::string, Network::DnsLookupFamily,
                                       std::list<std::string>, std::list<std::string>>;
std::vector<RedisDnsConfigTuple> generateRedisDnsParams() {
  std::vector<RedisDnsConfigTuple> dns_config;
  {
    std::string family_yaml("");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    std::list<std::string> resolved_host{"127.0.0.1:22120", "127.0.0.2:22120"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response, resolved_host));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: V4_ONLY)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    std::list<std::string> resolved_host{"127.0.0.1:22120", "127.0.0.2:22120"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response, resolved_host));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: V6_ONLY)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V6Only);
    std::list<std::string> dns_response{"::1", "2001:0db8:85a3:0000:0000:8a2e:0370:7334"};
    std::list<std::string> resolved_host{"[::1]:22120", "[2001:db8:85a3::8a2e:370:7334]:22120"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response, resolved_host));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: AUTO)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"::1", "2001:0db8:85a3:0000:0000:8a2e:0370:7334"};
    std::list<std::string> resolved_host{"[::1]:22120", "[2001:db8:85a3::8a2e:370:7334]:22120"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response, resolved_host));
  }
  return dns_config;
}

class RedisDnsParamTest : public RedisClusterTest,
                          public testing::WithParamInterface<RedisDnsConfigTuple> {};

INSTANTIATE_TEST_SUITE_P(DnsParam, RedisDnsParamTest, testing::ValuesIn(generateRedisDnsParams()));

// Validate that if the DNS and CLUSTER SLOT resolve immediately, we have the expected
// host state and initialization callback invocation.

TEST_P(RedisDnsParamTest, ImmediateResolveDns) {
  const std::string config = R"EOF(
  name: name
  connect_timeout: 0.25s
  )EOF" + std::get<0>(GetParam()) +
                             R"EOF(
  load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
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

  setupFromV3Yaml(config);

  expectRedisResolve(true);
  EXPECT_CALL(*dns_resolver_, resolve("foo.bar.com", std::get<1>(GetParam()), _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        std::list<std::string> address_pair = std::get<2>(GetParam());
        cb(Network::DnsResolver::ResolutionStatus::Success,
           TestUtility::makeDnsResponse(address_pair));
        EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
        expectClusterSlotResponse(
            singleSlotPrimaryReplica(address_pair.front(), address_pair.back(), 22120));
        return nullptr;
      }));

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  cluster_->initialize([&]() -> void { initialized_.ready(); });

  expectHealthyHosts(std::get<3>(GetParam()));
}

TEST_F(RedisClusterTest, EmptyDnsResponse) {
  Event::MockTimer* dns_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  setupFromV3Yaml(BasicConfig);
  const std::list<std::string> resolved_addresses{};
  EXPECT_CALL(*dns_timer, enableTimer(_, _));
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses);

  EXPECT_CALL(initialized_, ready());
  cluster_->initialize([&]() -> void { initialized_.ready(); });

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1U, cluster_->info()->stats().update_empty_.value());

  // Does not recreate the timer on subsequent DNS resolve calls.
  EXPECT_CALL(*dns_timer, enableTimer(_, _));
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses);
  dns_timer->invokeCallback();

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(2U, cluster_->info()->stats().update_empty_.value());
}

TEST_F(RedisClusterTest, FailedDnsResponse) {
  Event::MockTimer* dns_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  setupFromV3Yaml(BasicConfig);
  const std::list<std::string> resolved_addresses{};
  EXPECT_CALL(*dns_timer, enableTimer(_, _));
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses,
                         Network::DnsResolver::ResolutionStatus::Failure);

  EXPECT_CALL(initialized_, ready());
  cluster_->initialize([&]() -> void { initialized_.ready(); });

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0U, cluster_->info()->stats().update_empty_.value());

  // Does not recreate the timer on subsequent DNS resolve calls.
  EXPECT_CALL(*dns_timer, enableTimer(_, _));
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses);
  dns_timer->invokeCallback();

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1U, cluster_->info()->stats().update_empty_.value());
}

TEST_F(RedisClusterTest, Basic) {
  // Using load assignment.
  const std::string basic_yaml_load_assignment = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  load_assignment:
    cluster_name: name
    endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: foo.bar.com
                port_value: 22120
            health_check_config:
              port_value: 8000
  cluster_type:
    name: envoy.clusters.redis
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";

  testBasicSetup(BasicConfig, "foo.bar.com");
  testBasicSetup(basic_yaml_load_assignment, "foo.bar.com");

  // Exercise stubbed out interfaces for coverage.
  exerciseStubs();
}

TEST_F(RedisClusterTest, RedisResolveFailure) {
  setupFromV3Yaml(BasicConfig);
  const std::list<std::string> resolved_addresses{"127.0.0.1", "127.0.0.2"};
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses);
  expectRedisResolve(true);

  cluster_->initialize([&]() -> void { initialized_.ready(); });

  // Initialization will wait til the redis cluster succeed.
  expectClusterSlotFailure();
  EXPECT_EQ(1U, cluster_->info()->stats().update_attempt_.value());
  EXPECT_EQ(1U, cluster_->info()->stats().update_failure_.value());

  expectRedisResolve(true);
  resolve_timer_->invokeCallback();
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
  expectClusterSlotResponse(singleSlotPrimaryReplica("127.0.0.1", "127.0.0.2", 22120));
  expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));

  // Expect no change if resolve failed.
  expectRedisResolve();
  resolve_timer_->invokeCallback();
  expectClusterSlotFailure();
  expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));
  EXPECT_EQ(3U, cluster_->info()->stats().update_attempt_.value());
  EXPECT_EQ(2U, cluster_->info()->stats().update_failure_.value());
}

TEST_F(RedisClusterTest, FactoryInitNotRedisClusterTypeFailure) {
  const std::string basic_yaml_hosts = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 22120
  cluster_type:
    name: envoy.clusters.memcached
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFactoryFromV2Yaml(basic_yaml_hosts), EnvoyException,
                            "Redis cluster can only created with redis cluster type.");
}

TEST_F(RedisClusterTest, FactoryInitRedisClusterTypeSuccess) {
  setupFactoryFromV2Yaml(BasicConfig);
}

TEST_F(RedisClusterTest, RedisErrorResponse) {
  setupFromV3Yaml(BasicConfig);
  const std::list<std::string> resolved_addresses{"127.0.0.1", "127.0.0.2"};
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses);
  expectRedisResolve(true);

  cluster_->initialize([&]() -> void { initialized_.ready(); });

  // Initialization will wait til the redis cluster succeed.
  std::vector<NetworkFilters::Common::Redis::RespValue> hello_world(2);
  hello_world[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  hello_world[0].asString() = "hello";
  hello_world[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  hello_world[1].asString() = "world";

  NetworkFilters::Common::Redis::RespValuePtr hello_world_response(
      new NetworkFilters::Common::Redis::RespValue());
  hello_world_response->type(NetworkFilters::Common::Redis::RespType::Array);
  hello_world_response->asArray().swap(hello_world);

  EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(0);
  expectClusterSlotResponse(std::move(hello_world_response));
  EXPECT_EQ(1U, cluster_->info()->stats().update_attempt_.value());
  EXPECT_EQ(1U, cluster_->info()->stats().update_failure_.value());

  expectRedisResolve();
  resolve_timer_->invokeCallback();
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
  std::bitset<ResponseFlagSize> single_slot_primary(0xfff);
  std::bitset<ResponseReplicaFlagSize> no_replica(0);
  expectClusterSlotResponse(createResponse(single_slot_primary, no_replica));
  expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120"}));

  // Expect no change if resolve failed.
  uint64_t update_attempt = 2;
  uint64_t update_failure = 1;
  // Test every combination the cluster slots response.
  for (uint64_t i = 0; i < (1 << ResponseFlagSize); i++) {
    std::bitset<ResponseFlagSize> flags(i);
    expectRedisResolve();
    resolve_timer_->invokeCallback();
    if (flags.all()) {
      EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1).WillOnce(Return(false));
    }
    expectClusterSlotResponse(createResponse(flags, no_replica));
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120"}));
    EXPECT_EQ(++update_attempt, cluster_->info()->stats().update_attempt_.value());
    if (!flags.all()) {
      EXPECT_EQ(++update_failure, cluster_->info()->stats().update_failure_.value());
    }
  }
}

TEST_F(RedisClusterTest, RedisReplicaErrorResponse) {
  setupFromV3Yaml(BasicConfig);
  const std::list<std::string> resolved_addresses{"127.0.0.1", "127.0.0.2"};
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses);
  expectRedisResolve(true);

  cluster_->initialize([&]() -> void { initialized_.ready(); });

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
  std::bitset<ResponseFlagSize> single_slot_primary(0xfff);
  std::bitset<ResponseReplicaFlagSize> no_replica(0);
  expectClusterSlotResponse(createResponse(single_slot_primary, no_replica));
  expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120"}));

  // Expect no change if resolve failed.
  uint64_t update_attempt = 1;
  uint64_t update_failure = 0;
  // Test every combination the replica error response.
  for (uint64_t i = 1; i < (1 << ResponseReplicaFlagSize); i++) {
    std::bitset<ResponseReplicaFlagSize> replica_flags(i);
    expectRedisResolve();
    resolve_timer_->invokeCallback();
    if (replica_flags.all()) {
      EXPECT_CALL(membership_updated_, ready());
      EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1).WillOnce(Return(false));
    }
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120"}));
    expectClusterSlotResponse(createResponse(single_slot_primary, replica_flags));
    EXPECT_EQ(++update_attempt, cluster_->info()->stats().update_attempt_.value());
    if (!(replica_flags.all() || replica_flags.none())) {
      EXPECT_EQ(++update_failure, cluster_->info()->stats().update_failure_.value());
    }
  }
}

TEST_F(RedisClusterTest, DnsDiscoveryResolverBasic) {
  setupFromV3Yaml(BasicConfig);
  testDnsResolve("foo.bar.com", 22120);
}

TEST_F(RedisClusterTest, MultipleDnsDiscovery) {
  const std::string config = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 22120               
            - endpoint:
                address:
                  socket_address:
                    address: foo1.bar.com
                    port_value: 22120
  cluster_type:
    name: envoy.clusters.redis
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";

  setupFromV3Yaml(config);

  // Only single in-flight "cluster slots" call.
  expectRedisResolve(true);

  ReadyWatcher dns_resolve_1;
  ReadyWatcher dns_resolve_2;

  EXPECT_CALL(*dns_resolver_, resolve("foo.bar.com", _, _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        cb(Network::DnsResolver::ResolutionStatus::Success,
           TestUtility::makeDnsResponse(std::list<std::string>({"127.0.0.1", "127.0.0.2"})));
        return nullptr;
      }));

  EXPECT_CALL(*dns_resolver_, resolve("foo1.bar.com", _, _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        cb(Network::DnsResolver::ResolutionStatus::Success,
           TestUtility::makeDnsResponse(std::list<std::string>({"127.0.0.3", "127.0.0.4"})));
        return nullptr;
      }));

  cluster_->initialize([&]() -> void { initialized_.ready(); });

  // Pending RedisResolve will call cancel in the destructor.
  EXPECT_CALL(pool_request_, cancel());
}

TEST_F(RedisClusterTest, HostRemovalAfterHcFail) {
  setupFromV3Yaml(BasicConfig);
  auto health_checker = std::make_shared<Upstream::MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  const std::list<std::string> resolved_addresses{"127.0.0.1", "127.0.0.2"};
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses);
  expectRedisResolve(true);

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  cluster_->initialize([&]() -> void { initialized_.ready(); });

  EXPECT_CALL(*cluster_callback_, onClusterSlotUpdate(_, _)).Times(1);
  expectClusterSlotResponse(singleSlotPrimaryReplica("127.0.0.1", "127.0.0.2", 22120));

  // Verify that both hosts are initially marked with FAILED_ACTIVE_HC, then
  // clear the flag to simulate that these hosts have been successfully health
  // checked.
  {
    EXPECT_CALL(membership_updated_, ready());
    const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2UL, hosts.size());

    for (size_t i = 0; i < 2; ++i) {
      EXPECT_TRUE(hosts[i]->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
      hosts[i]->healthFlagClear(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
      hosts[i]->healthFlagClear(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
      health_checker->runCallbacks(hosts[i], Upstream::HealthTransition::Changed);
    }
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));
  }

  // Failed HC
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(*cluster_callback_, onHostHealthUpdate());
  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  hosts[1]->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(hosts[1], Upstream::HealthTransition::Changed);

  EXPECT_THAT(2U, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_THAT(1U, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
