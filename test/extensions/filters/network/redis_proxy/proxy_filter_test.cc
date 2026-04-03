#include <chrono>
#include <memory>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/network/redis_proxy/config.h"
#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByRef;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::WithArg;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

envoy::extensions::filters::network::redis_proxy::v3::RedisProxy
parseProtoFromYaml(const std::string& yaml_string) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy config;
  TestUtility::loadFromYaml(yaml_string, config);
  return config;
}

class RedisProxyFilterConfigTest
    : public testing::Test,
      public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};
  Stats::TestUtil::TestStore store_;
  Network::MockDrainDecision drain_decision_;
  Runtime::MockLoader runtime_;
  NiceMock<Api::MockApi> api_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Event::SimulatedTimeSystem time_source_;
};

TEST_F(RedisProxyFilterConfigTest, Normal) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_EQ("redis.foo.", config.stat_prefix_);
  EXPECT_TRUE(config.downstream_auth_username_.empty());
  EXPECT_TRUE(config.downstream_auth_passwords_.empty());
}

TEST_F(RedisProxyFilterConfigTest, BadRedisProxyConfig) {
  const std::string yaml_string = R"EOF(
  cluster_name: fake_cluster
  cluster: fake_cluster
  )EOF";

  EXPECT_THROW(parseProtoFromYaml(yaml_string), EnvoyException);
}

TEST_F(RedisProxyFilterConfigTest, DownstreamAuthPasswordSet) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_password:
    inline_string: somepassword
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 1);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "somepassword");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamMultipleAuthPasswordsSet) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_password:
    inline_string: somepassword
  downstream_auth_passwords:
  - inline_string: newpassword1
  - inline_string: newpassword2
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 3);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "somepassword");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[2], "newpassword2");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamOnlyExraAuthPasswordsSet) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_passwords:
  - inline_string: newpassword1
  - inline_string: newpassword2
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 2);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword2");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamAuthAclSet) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_username:
    inline_string: someusername
  downstream_auth_password:
    inline_string: somepassword
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_EQ(config.downstream_auth_username_, "someusername");
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 1);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "somepassword");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamAuthAclSetWithMultiplePasswords) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_username:
    inline_string: someusername
  downstream_auth_password:
    inline_string: somepassword
  downstream_auth_passwords:
  - inline_string: newpassword1
  - inline_string: newpassword2
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_EQ(config.downstream_auth_username_, "someusername");
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 3);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "somepassword");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[2], "newpassword2");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamAuthAclSetWithOnlyExtraPasswords) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_username:
    inline_string: someusername
  downstream_auth_passwords:
  - inline_string: newpassword1
  - inline_string: newpassword2
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_EQ(config.downstream_auth_username_, "someusername");
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 2);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword2");
}

TEST_F(RedisProxyFilterConfigTest, ExternalAuthBasic) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  external_auth_provider:
    grpc_service:
      envoy_grpc:
        cluster_name: fake_cluster
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_TRUE(config.external_auth_enabled_);
  EXPECT_FALSE(config.external_auth_expiration_enabled_);
}

TEST_F(RedisProxyFilterConfigTest, ExternalAuthWithExpiration) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  external_auth_provider:
    grpc_service:
      envoy_grpc:
        cluster_name: fake_cluster
    enable_auth_expiration: true
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {});
  EXPECT_TRUE(config.external_auth_enabled_);
  EXPECT_TRUE(config.external_auth_expiration_enabled_);
}

// Dynamic clusterRespVersion() tests. The cap is no longer captured at filter
// load time; it is read live on each call from the worker's thread-local
// cluster manager state. This block pins three properties:
//   1. Unknown cluster (LDS-before-CDS) caps to RESP2 and self-heals when
//      the cluster later arrives.
//   2. Cluster RESP3 → RESP2 downgrade is reflected immediately.
//   3. Mirror clusters are NOT in response_bearing_clusters_ and therefore
//      do not constrain the cap.
namespace {
// Helper: install ON_CALL on the cluster manager's getThreadLocalCluster so
// it returns either nullptr or a pre-built ThreadLocalCluster whose info
// carries the requested upstream RESP version. Tests then call
// clusterRespVersion() and assert the floor.
void installCluster(Upstream::MockClusterManager& cm, const std::string& name,
                    Upstream::ThreadLocalCluster* tlc) {
  ON_CALL(cm, getThreadLocalCluster(absl::string_view(name))).WillByDefault(Return(tlc));
}

std::shared_ptr<NiceMock<Upstream::MockClusterInfo>>
makeRespClusterInfo(envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions::
                        UpstreamProtocol::Version version) {
  auto info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions proto_opts;
  proto_opts.mutable_upstream_protocol()->set_version(version);
  auto options = std::make_shared<ProtocolOptionsConfigImpl>(proto_opts);
  ON_CALL(*info, extensionProtocolOptions(NetworkFilterNames::get().RedisProxy))
      .WillByDefault(Return(options));
  return info;
}
} // namespace

TEST_F(RedisProxyFilterConfigTest, ClusterRespVersionUnknownClusterCapsToResp2) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: missing_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  // No cluster installed → getThreadLocalCluster returns nullptr → cap == 2.
  installCluster(cluster_manager_, "missing_cluster", nullptr);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {"missing_cluster"});
  EXPECT_EQ(2U, config.clusterRespVersion());
}

TEST_F(RedisProxyFilterConfigTest, ClusterRespVersionSelfHealsWhenClusterArrives) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: late_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  // Initially missing.
  installCluster(cluster_manager_, "late_cluster", nullptr);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {"late_cluster"});
  EXPECT_EQ(2U, config.clusterRespVersion());

  // CDS arrives carrying RESP3 — next clusterRespVersion() reflects it.
  NiceMock<Upstream::MockThreadLocalCluster> tlc;
  auto info = makeRespClusterInfo(envoy::extensions::filters::network::redis_proxy::v3::
                                      RedisProtocolOptions::UpstreamProtocol::RESP3);
  ON_CALL(tlc, info()).WillByDefault(Return(info));
  installCluster(cluster_manager_, "late_cluster", &tlc);
  EXPECT_EQ(3U, config.clusterRespVersion());
}

TEST_F(RedisProxyFilterConfigTest, ClusterRespVersionReflectsDowngrade) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: shifting_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  // Initially RESP3.
  NiceMock<Upstream::MockThreadLocalCluster> tlc;
  auto info_v3 = makeRespClusterInfo(envoy::extensions::filters::network::redis_proxy::v3::
                                         RedisProtocolOptions::UpstreamProtocol::RESP3);
  ON_CALL(tlc, info()).WillByDefault(Return(info_v3));
  installCluster(cluster_manager_, "shifting_cluster", &tlc);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {"shifting_cluster"});
  EXPECT_EQ(3U, config.clusterRespVersion());

  // Cluster info flips to RESP2 (e.g. CDS update) — cap drops immediately.
  auto info_v2 = makeRespClusterInfo(envoy::extensions::filters::network::redis_proxy::v3::
                                         RedisProtocolOptions::UpstreamProtocol::RESP2);
  ON_CALL(tlc, info()).WillByDefault(Return(info_v2));
  EXPECT_EQ(2U, config.clusterRespVersion());
}

TEST_F(RedisProxyFilterConfigTest, ClusterRespVersionExcludesMirrorClusters) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: primary_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  // Build response-bearing list with ONLY the primary (RESP3). A mirror
  // cluster on RESP2 would NOT appear here — that is the contract pinned
  // by addResponseBearingClusters() vs addAllReferencedClusters() in
  // config.cc. Direct ProxyFilterConfig construction below mirrors what the
  // factory hands in: response-bearing only.
  NiceMock<Upstream::MockThreadLocalCluster> primary_tlc;
  auto primary_info = makeRespClusterInfo(envoy::extensions::filters::network::redis_proxy::v3::
                                              RedisProtocolOptions::UpstreamProtocol::RESP3);
  ON_CALL(primary_tlc, info()).WillByDefault(Return(primary_info));
  installCluster(cluster_manager_, "primary_cluster", &primary_tlc);

  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_, {"primary_cluster"});
  // Cap is RESP3 — a hypothetical RESP2 mirror cluster is intentionally
  // absent from the response_bearing_clusters_ list and cannot drag the
  // cap down.
  EXPECT_EQ(3U, config.clusterRespVersion());
}

TEST_F(RedisProxyFilterConfigTest, ClusterRespVersionFloorsAcrossResponseBearingClusters) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: primary_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  // Two response-bearing clusters: primary RESP3, read policy RESP2 → floor 2.
  NiceMock<Upstream::MockThreadLocalCluster> primary_tlc;
  NiceMock<Upstream::MockThreadLocalCluster> read_tlc;
  auto primary_info = makeRespClusterInfo(envoy::extensions::filters::network::redis_proxy::v3::
                                              RedisProtocolOptions::UpstreamProtocol::RESP3);
  auto read_info = makeRespClusterInfo(envoy::extensions::filters::network::redis_proxy::v3::
                                           RedisProtocolOptions::UpstreamProtocol::RESP2);
  ON_CALL(primary_tlc, info()).WillByDefault(Return(primary_info));
  ON_CALL(read_tlc, info()).WillByDefault(Return(read_info));
  installCluster(cluster_manager_, "primary_cluster", &primary_tlc);
  installCluster(cluster_manager_, "read_cluster", &read_tlc);

  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this, cluster_manager_,
                           {"primary_cluster", "read_cluster"});
  EXPECT_EQ(2U, config.clusterRespVersion());
}

class RedisProxyFilterTest
    : public testing::Test,
      public Common::Redis::DecoderFactory,
      public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};

  static constexpr const char* DefaultConfig = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";

  RedisProxyFilterTest(const std::string& yaml_string, bool defer_filter_construction = false) {
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
        parseProtoFromYaml(yaml_string);
    // Pass empty response_bearing_clusters: dynamic clusterRespVersion()
    // returns RESP2 in that case. The proxy_filter unit tests use a mock
    // splitter (MockInstance), so the splitter never reads the cap; the
    // value here is inert for these tests.
    config_ = std::make_shared<ProxyFilterConfig>(
        proto_config, *store_.rootScope(), drain_decision_, runtime_, api_, time_source_, *this,
        cluster_manager_, std::vector<std::string>{});
    time_source_.setSystemTime(std::chrono::seconds(0));
    if (defer_filter_construction) {
      // Caller will construct the filter explicitly via buildFilter() — used when a test needs
      // to install EXPECT_CALL on encoder_ before the filter ctor runs setProtocolVersion.
      return;
    }
    buildFilter();
  }

  RedisProxyFilterTest() : RedisProxyFilterTest(DefaultConfig) {}

  // Construct the ProxyFilter using the previously-built config_. Split
  // out of the ctor so tests can wire EXPECT_CALL on encoder_ first when the
  // ctor's setProtocolVersion flip is the property under test.
  void buildFilter() {
    if (config_->external_auth_enabled_) {
      external_auth_client_ = new ExternalAuth::MockExternalAuthClient();
      filter_ = std::make_unique<ProxyFilter>(
          *this, Common::Redis::EncoderPtr{encoder_}, splitter_, config_,
          ExternalAuth::ExternalAuthClientPtr{external_auth_client_});
    } else {
      filter_ = std::make_unique<ProxyFilter>(*this, Common::Redis::EncoderPtr{encoder_}, splitter_,
                                              config_, nullptr);
    }

    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
    EXPECT_EQ(1UL, config_->stats_.downstream_cx_total_.value());
    EXPECT_EQ(1UL, config_->stats_.downstream_cx_active_.value());

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  ~RedisProxyFilterTest() override {
    filter_.reset();
    for (const Stats::GaugeSharedPtr& gauge : store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  // Common::Redis::DecoderFactory
  Common::Redis::DecoderPtr create(Common::Redis::DecoderCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    return Common::Redis::DecoderPtr{decoder_};
  }

  // NiceMock suppresses the "uninteresting call" warning for tests that do not specifically
  // EXPECT_CALL on setProtocolVersion. The ProxyFilter ctor unconditionally calls
  // setProtocolVersion(...) to apply the configured initial RESP version; without NiceMock,
  // every existing test that did not opt in to encoder-version assertions would fail. ON_CALL
  // inside MockEncoder::MockEncoder() still forwards the call to the real encoder, so wire-
  // output tests are unaffected.
  NiceMock<Common::Redis::MockEncoder>* encoder_{new NiceMock<Common::Redis::MockEncoder>()};
  Common::Redis::MockDecoder* decoder_{new Common::Redis::MockDecoder()};
  Common::Redis::DecoderCallbacks* decoder_callbacks_{};
  CommandSplitter::MockInstance splitter_;
  Stats::TestUtil::TestStore store_;
  NiceMock<Network::MockDrainDecision> drain_decision_;
  NiceMock<Runtime::MockLoader> runtime_;
  ProxyFilterConfigSharedPtr config_;
  std::unique_ptr<ProxyFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Api::MockApi> api_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  ExternalAuth::MockExternalAuthClient* external_auth_client_;
  Event::SimulatedTimeSystem time_source_;
};

class RedisProxyFilterTestWithTwoCallbacks : public RedisProxyFilterTest {
public:
  CommandSplitter::MockSplitRequest* request_handle1_{new CommandSplitter::MockSplitRequest()};
  CommandSplitter::MockSplitRequest* request_handle2_{new CommandSplitter::MockSplitRequest()};
  CommandSplitter::SplitCallbacks* request_callbacks1_;
  CommandSplitter::SplitCallbacks* request_callbacks2_;

  void decodeHelper(Buffer::Instance&) {
    Common::Redis::RespValuePtr request1(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _, _, _))
        .WillOnce(
            DoAll(WithArg<1>(SaveArgAddress(&request_callbacks1_)), Return(request_handle1_)));
    decoder_callbacks_->onRespValue(std::move(request1));

    Common::Redis::RespValuePtr request2(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request2), _, _, _))
        .WillOnce(
            DoAll(WithArg<1>(SaveArgAddress(&request_callbacks2_)), Return(request_handle2_)));
    decoder_callbacks_->onRespValue(std::move(request2));
  }
};

TEST_F(RedisProxyFilterTestWithTwoCallbacks, OutOfOrderResponseWithDrainClose) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke(this, &RedisProxyFilterTestWithTwoCallbacks::decodeHelper));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  EXPECT_EQ(2UL, config_->stats_.downstream_rq_total_.value());
  EXPECT_EQ(2UL, config_->stats_.downstream_rq_active_.value());

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  Common::Redis::RespValue* response2_ptr = response2.get();
  request_callbacks2_->onResponse(std::move(response2));

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  EXPECT_CALL(*encoder_, encode(Ref(*response1), _));
  EXPECT_CALL(*encoder_, encode(Ref(*response2_ptr), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::All)).WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("redis.drain_close_enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  request_callbacks1_->onResponse(std::move(response1));

  EXPECT_EQ(1UL, config_->stats_.downstream_cx_drain_close_.value());
}

TEST_F(RedisProxyFilterTestWithTwoCallbacks, OutOfOrderResponseDownstreamDisconnectBeforeFlush) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke(this, &RedisProxyFilterTestWithTwoCallbacks::decodeHelper));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  EXPECT_EQ(2UL, config_->stats_.downstream_rq_total_.value());
  EXPECT_EQ(2UL, config_->stats_.downstream_rq_active_.value());

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  request_callbacks2_->onResponse(std::move(response2));
  EXPECT_CALL(*request_handle1_, cancel());

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, DownstreamDisconnectWithActive) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* request_handle1 = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* request_callbacks1;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr request1(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&request_callbacks1)), Return(request_handle1)));
    decoder_callbacks_->onRespValue(std::move(request1));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  EXPECT_CALL(*request_handle1, cancel());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, ImmediateResponse) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request1(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request1));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        Common::Redis::RespValuePtr error(new Common::Redis::RespValue());
        error->type(Common::Redis::RespType::Error);
        error->asString() = "no healthy upstream";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*error)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(error));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, ProtocolError) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    throw Common::Redis::ProtocolError("error");
  }));

  Common::Redis::RespValue error;
  error.type(Common::Redis::RespType::Error);
  error.asString() = "downstream protocol error";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(error)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_data, false));

  EXPECT_EQ(1UL, store_.counter("redis.foo.downstream_cx_protocol_error").value());
}

TEST_F(RedisProxyFilterTest, AuthWhenNotRequired) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_TRUE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr error(new Common::Redis::RespValue());
        error->type(Common::Redis::RespType::Error);
        error->asString() = "ERR Client sent AUTH, but no password is set";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*error)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("foo");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterTest, AuthAclWhenNotRequired) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_TRUE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr error(new Common::Redis::RespValue());
        error->type(Common::Redis::RespType::Error);
        error->asString() = "ERR Client sent AUTH, but no username-password pair is set";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*error)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("foo", "bar");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string downstream_auth_password_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
downstream_auth_password:
  inline_string: somepassword
)EOF";

class RedisProxyFilterWithAuthPasswordTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithAuthPasswordTest() : RedisProxyFilterTest(downstream_auth_password_config) {}
};

TEST_F(RedisProxyFilterWithAuthPasswordTest, AuthPasswordCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("somepassword");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthPasswordTest, AuthPasswordIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR invalid password";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string downstream_multiple_auth_passwords_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
downstream_auth_password:
  inline_string: somepassword
downstream_auth_passwords:
-  inline_string: newpassword1
-  inline_string: newpassword2
)EOF";

class RedisProxyFilterWithMultipleAuthPasswordsTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithMultipleAuthPasswordsTest()
      : RedisProxyFilterTest(downstream_multiple_auth_passwords_config) {}
};

TEST_F(RedisProxyFilterWithMultipleAuthPasswordsTest, AuthPasswordCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("somepassword");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithMultipleAuthPasswordsTest, AuthNewPassword1Correct) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("newpassword1");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithMultipleAuthPasswordsTest, AuthNewPassword2Correct) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("newpassword2");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithMultipleAuthPasswordsTest, AuthPasswordIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR invalid password";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string downstream_auth_acl_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
downstream_auth_username:
  inline_string: someusername
downstream_auth_password:
  inline_string: somepassword
)EOF";

class RedisProxyFilterWithAuthAclTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithAuthAclTest() : RedisProxyFilterTest(downstream_auth_acl_config) {}
};

TEST_F(RedisProxyFilterWithAuthAclTest, AuthAclCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "somepassword");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclTest, AuthAclUsernameIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongusername", "somepassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclTest, AuthAclPasswordIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string downstream_auth_acl_multiple_passwords_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
downstream_auth_username:
  inline_string: someusername
downstream_auth_password:
  inline_string: somepassword
downstream_auth_passwords:
-  inline_string: newpassword1
-  inline_string: newpassword2
)EOF";

class RedisProxyFilterWithAuthAclMultiplePasswordsTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithAuthAclMultiplePasswordsTest()
      : RedisProxyFilterTest(downstream_auth_acl_multiple_passwords_config) {}
};

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "somepassword");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclCorrect1) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "newpassword1");
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclCorrect2) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "newpassword2");
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclUsernameIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongusername", "somepassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclPasswordIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string external_auth_expiration_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
external_auth_provider:
  grpc_service:
    envoy_grpc:
      cluster_name: fake_cluster
  enable_auth_expiration: true
)EOF";

class RedisProxyFilterWithExternalAuthAndExpiration : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithExternalAuthAndExpiration()
      : RedisProxyFilterTest(external_auth_expiration_config) {}
};

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthPasswordWrong) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_,
                    authenticateExternal(_, _, _, EMPTY_STRING, "wrongpassword"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Unauthorized;
                  auth_response->message = "sorry, invalid password";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR sorry, invalid password";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthUsernamePasswordWrong) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_,
                    authenticateExternal(_, _, _, "wrongusername", "wrongpassword"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Unauthorized;
                  auth_response->message = "ooops, invalid password and username";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR ooops, invalid password and username";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongusername", "wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthUsernamePasswordCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "username", "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(12);

                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("username", "password");
        // callbacks can be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthPasswordCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(12);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("password");
        // callbacks can be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthWithPipelining) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr auth_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(auth_request));
  }));

  // AUTH expectation
  EXPECT_CALL(splitter_, makeRequest_(Ref(*auth_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // External auth expectation.
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(12);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  // Expect that we receive OK from AUTH first
                  Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
                  reply->type(Common::Redis::RespType::SimpleString);
                  reply->asString() = "OK";
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // PING expectation, will be processed after AUTH.
                  Common::Redis::RespValuePtr ping_request(new Common::Redis::RespValue());
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*ping_request), _, _, _))
                      .WillOnce(Invoke(
                          [&](const Common::Redis::RespValue&,
                              CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                              const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                            // Having connection allowed proves pipelining was respected.
                            EXPECT_TRUE(filter_->connectionAllowed());

                            Common::Redis::RespValuePtr reply_ping(new Common::Redis::RespValue());
                            reply_ping->type(Common::Redis::RespType::SimpleString);
                            reply_ping->asString() = "PONG";
                            EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply_ping)), _));
                            EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                            callbacks.onResponse(std::move(reply_ping));
                            return nullptr;
                          }));

                  // Simulate that before the auth response is received, another command is
                  // pipelined.
                  decoder_callbacks_->onRespValue(std::move(ping_request));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));

        callbacks.onAuth("password");
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthPasswordCorrectButThenExpires) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("password");
        // callbacks can be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        // but then expiration passes
        time_source_.advanceTimeWait(std::chrono::hours(2));
        // and callbacks are not accessible anymore.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthError) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Error;
                  auth_response->message = "sorry, error";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR external authentication failed";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("password");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// When HELLO 3 flips the downstream version mid-request, the HELLO reply itself must be
// encoded in the newly negotiated version. The hazard guarded against is
// PendingRequest::resp_version_at_creation_ being stamped at request construction (RESP2) and
// never updated: ProxyFilter::onResponse's stamp-vs-current branch would then flip the encoder
// back to RESP2 and emit the Map reply as a flat *2N array, which strict RESP3 clients
// (Lettuce, redis-py protocol=3) reject. setDownstreamRespVersion updates the stamp so the
// loop sees stamp==current and emits the Map natively.
TEST_F(RedisProxyFilterTest, HelloReplyEncodedInNegotiatedVersion) {
  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  Common::Redis::RespValue* request_ptr = request.get();

  Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
  reply->type(Common::Redis::RespType::Map);
  Common::Redis::RespValue* reply_ptr = reply.get();

  // Key assertion: the encoder must NEVER be flipped back to RESP2 during this test. A single
  // Resp2 call signals the stale-stamp regression.
  EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp2)).Times(0);
  // The initial flip to RESP3 (from setDownstreamRespVersion) must happen exactly once.
  EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
  EXPECT_CALL(*encoder_, encode(Ref(*reply_ptr), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request_ptr), _, _, _))
      .WillOnce(Invoke([&reply](const Common::Redis::RespValue&,
                                CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                                const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // Simulate the HELLO 3 splitter sequence:
        //   1. flip downstream version to RESP3
        //   2. synthesize the Map reply and deliver it via onResponse
        // The fix's update to resp_version_at_creation_ happens inside
        // setDownstreamRespVersion; subsequent onResponse → encode loop
        // sees stamp==current and skips the flip-encode-flip sandwich.
        callbacks.setDownstreamRespVersion(3);
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// HELLO N AUTH ... with external auth: the splitter delegates the inline-auth check via
// attemptDownstreamAuthInline; ProxyFilter routes to authenticateExternal and stashes the
// requested protocol version on the PendingRequest. On Authorized the deferred reply is the
// HELLO Map (RESP3 native shape when version=3), encoded after setDownstreamRespVersion has
// flipped the encoder. Verifies the end-to-end path that the splitter test only pins at the
// no-emit / Pending boundary.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthExternalAuthAuthorizedEmitsHelloMap) {
  InSequence s;

  // The deferred HELLO Map must match what buildHelloReply(3) produces — same shape that the
  // synchronous-allowed path would emit. Comparing against the canonical builder pins both the
  // deferred-emission contract and the version-flip ordering: ByRef does pointer-equal
  // comparison through Eq's operator==, which on RespValue checks structural equality.
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(12);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        // Encoder must flip to RESP3 (the negotiated version) BEFORE the HELLO Map is encoded
        // — otherwise the Map down-converts to a flat *2N array on the wire and a strict
        // RESP3 client rejects the shape. NiceMock's ON_CALL forwards encode() to the real
        // encoder so encoder_buffer_ fills and connection.write fires.
        EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Pending,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// HELLO N AUTH ... + external auth Unauthorized: deferred reply is WRONGPASS (matching the
// splitter's local-auth Denied shape so RESP3 clients see a uniform error code).
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthExternalAuthUnauthorizedEmitsWrongpass) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "wrong"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Unauthorized;
                  auth_response->message = "rejected";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS rejected";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Pending,
                  callbacks.attemptDownstreamAuthInline("alice", "wrong", 3));
        // Failed HELLO AUTH must NOT flip connection_allowed_ to true. A subsequent ordinary
        // command on this connection still hits the auth gate.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Pipelined HELLO N AUTH ... + GET in the same TCP buffer: the GET must be held by the
// existing pending_request_value_ queue (external_auth_call_status_ = Pending) until the auth
// round trip completes, then dispatched. Otherwise the GET would land on the wire ahead of
// the HELLO Map and the upstream / downstream RESP versions would mismatch. Mirrors the
// ExternalAuthWithPipelining shape for the AUTH-command path: simulate the second decode
// INSIDE the auth callback (so it lands while state=Pending) before firing the auth result.
//
// Also pins the version-stamping fix in onAuthenticateExternal: a request decoded while
// auth was Pending was stamped with the pre-HELLO downstream version (RESP2). Without
// re-stamping, ProxyFilter::onResponse would do setProtocolVersion(Resp2) → encode →
// setProtocolVersion(Resp3) for the held GET, so the client would see a RESP2 reply on a
// freshly-negotiated RESP3 connection. The Times(0) on Resp2 below catches that regression.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthPipelinedRequestHeldUntilAuthCompletes) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));

  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // The encoder must NEVER be flipped back to RESP2 anywhere in this test flow. The
        // only legal flip is the single Resp3 transition driven by setDownstreamRespVersion
        // when the deferred HELLO Map is built. Any Resp2 flip means the held GET inherited
        // the stale RESP2 stamp and onResponse is doing the flip-encode-flip sandwich.
        EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp2))
            .Times(0);

        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  EXPECT_CALL(*encoder_,
                              setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // After the HELLO reply, the held GET is re-dispatched. By the time the
                  // splitter sees it, connection_allowed_ must already be true (HELLO AUTH
                  // succeeded), proving the pipelining gate respected ordering.
                  Common::Redis::RespValuePtr get_request(new Common::Redis::RespValue());
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*get_request), _, _, _))
                      .WillOnce(Invoke(
                          [&](const Common::Redis::RespValue&, CommandSplitter::SplitCallbacks& cb,
                              Event::Dispatcher&,
                              const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                            EXPECT_TRUE(filter_->connectionAllowed());
                            Common::Redis::RespValuePtr get_reply(new Common::Redis::RespValue());
                            get_reply->type(Common::Redis::RespType::BulkString);
                            get_reply->asString() = "value";
                            EXPECT_CALL(*encoder_, encode(Eq(ByRef(*get_reply)), _));
                            EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
                            cb.onResponse(std::move(get_reply));
                            return nullptr;
                          }));

                  // Simulate the pipelined GET arriving while external_auth_call_status_ is
                  // Pending: the decoder fires it before the auth callback resolves.
                  decoder_callbacks_->onRespValue(std::move(get_request));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Pending,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Connection close while a HELLO N AUTH ... external-auth round trip is pending: the filter
// must tear down cleanly without firing the deferred HELLO emission. The existing
// pending_requests_ cancellation path on RemoteClose handles this — verifies HELLO AUTH
// inherits that protection without code-path-specific breakage.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, HelloAuthPendingCanceledOnConnectionClose) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // Capture the auth-callback args but DO NOT fire the callback synchronously — the
        // round trip is "in flight" when the connection closes.
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Pending,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  // Connection close before the auth callback fires — onEvent's existing
  // external_auth_call_status_ branch invokes auth_client_->cancel() to release the in-flight
  // round trip, then drains pending_requests_. The HELLO PendingRequest's request_handle_ is
  // nullptr (the splitter returned nullptr in the Pending case) so the drain pop is a no-op
  // beyond destruction. No encode / write expectations are set: the filter must NOT emit
  // anything on the closed connection (the deferred HELLO Map would have raced the close).
  EXPECT_CALL(*external_auth_client_, cancel());
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// HELLO 3 AUTH ... + GET k1 + GET k2 pipelined together. After external auth succeeds, BOTH
// held GETs must be dispatched to the splitter. The earlier front-only drain loop dispatched
// only the first held entry: GET k1 went upstream and became the new front (no
// pending_request_value_), causing the loop to exit before reaching GET k2. resumeAuthHeldRequests
// scans the full list and dispatches every held entry.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthMultiplePipelinedRequestsAllResumed) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));

  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp2))
            .Times(0);
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  EXPECT_CALL(*encoder_,
                              setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // Both held GETs must hit the splitter. Each one's lambda holds the request
                  // upstream (returns a non-null handle) so neither gets popped by a
                  // synchronous onResponse — that exercises the pre-fix bug shape (after k1
                  // dispatched, it would still be the front with no pending_request_value_,
                  // so the front-only loop would have stopped).
                  Common::Redis::RespValuePtr get1_request(new Common::Redis::RespValue());
                  Common::Redis::RespValuePtr get2_request(new Common::Redis::RespValue());
                  auto get1_handle = new CommandSplitter::MockSplitRequest();
                  auto get2_handle = new CommandSplitter::MockSplitRequest();
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*get1_request), _, _, _))
                      .WillOnce(Return(get1_handle));
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*get2_request), _, _, _))
                      .WillOnce(Return(get2_handle));
                  // Both held handles get cancelled at filter teardown — pending_requests_
                  // still owns them when the connection mock is destroyed.
                  EXPECT_CALL(*get1_handle, cancel());
                  EXPECT_CALL(*get2_handle, cancel());

                  decoder_callbacks_->onRespValue(std::move(get1_request));
                  decoder_callbacks_->onRespValue(std::move(get2_request));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Pending,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
  // Drain pending_requests_ so ~ProxyFilter()'s ASSERT(pending_requests_.empty()) holds —
  // the two GETs are still upstream-in-flight at this point because their split-request
  // mocks return a handle without firing onResponse. RemoteClose cancels each handle.
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Older outstanding GET decoded BEFORE the HELLO AUTH (and still in flight when external auth
// completes) used to block the front-only drain loop: pending_requests_.front() was that
// older GET (no pending_request_value_), so the loop exited immediately and the held GET
// pipelined AFTER the HELLO never got resumed. The full-list scan walks past the in-flight
// entry and finds the held one.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthHeldRequestResumesEvenWithPriorOutstandingRequest) {
  InSequence s;

  // Hoist both upstream-in-flight handles to test scope so the cancel expectations can be
  // declared at the very end of the InSequence chain (after every other ordered expectation
  // has been declared and consumed). Otherwise an interleaved cancel expectation creates a
  // pre-requisite that the next decode/makeRequest cannot satisfy.
  auto* old_handle = new CommandSplitter::MockSplitRequest();
  auto* held_handle = new CommandSplitter::MockSplitRequest();

  // First onData: a regular GET that goes upstream and stays in flight.
  Buffer::OwnedImpl old_data;
  Common::Redis::RespValuePtr old_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(old_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(old_request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*old_request), _, _, _)).WillOnce(Return(old_handle));

  // Second onData: HELLO 3 AUTH ... that the splitter routes to external auth, plus a GET
  // that the decoder fires while external_auth_call_status_ is Pending so it lands in the
  // held queue with the older GET still in flight in front of it.
  Buffer::OwnedImpl new_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  Common::Redis::RespValuePtr held_get(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(new_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  // The HELLO Map is built and request.onResponse runs, but front-pop in
                  // onResponse stops at the older in-flight GET — the HELLO Map stays
                  // buffered behind that GET's not-yet-arrived response. setProtocolVersion
                  // (Resp3) still fires when setDownstreamRespVersion runs.
                  EXPECT_CALL(*encoder_,
                              setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));

                  // The held GET dispatched after HELLO is the property under test: even
                  // though front is the old in-flight GET (no pending_request_value_) and
                  // would have stopped the front-only loop, the full-list scan finds and
                  // dispatches the held GET.
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*held_get), _, _, _))
                      .WillOnce(Return(held_handle));

                  decoder_callbacks_->onRespValue(std::move(held_get));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Pending,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(old_data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(new_data, false));

  // Cancel expectations declared LAST, after every other sequence-bound expectation has been
  // declared (the inside-lambda ones run during the second onData). They fire in
  // pending_requests_ FIFO order during the RemoteClose drain: old_handle first, then
  // held_handle. The HELLO entry has no handle (splitter returned nullptr in the Pending
  // case) so no cancel for it.
  EXPECT_CALL(*old_handle, cancel());
  EXPECT_CALL(*held_handle, cancel());
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// HELLO N AUTH ... + AUTH bob secret pipelined: the second AUTH is held while the first
// external-auth round trip runs. When that round trip resolves and resumeAuthHeldRequests
// dispatches the held AUTH, the AUTH itself starts a SECOND external-auth round trip — the
// drain helper must observe external_auth_call_status_ = Pending again and stop, leaving any
// further held entries queued for the next onAuthenticateExternal to drain.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthResumedHeldAuthStartsSecondExternalAuth) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));

  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  EXPECT_CALL(*encoder_,
                              setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // Held AUTH that, when dispatched, triggers a second external-auth round
                  // trip. The mock invokes callbacks.onAuth("bob", "secret2") which forwards
                  // to ProxyFilter::onAuth → external auth Pending again.
                  Common::Redis::RespValuePtr held_auth(new Common::Redis::RespValue());
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*held_auth), _, _, _))
                      .WillOnce(Invoke(
                          [&](const Common::Redis::RespValue&, CommandSplitter::SplitCallbacks& cb,
                              Event::Dispatcher&,
                              const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                            EXPECT_CALL(*external_auth_client_,
                                        authenticateExternal(_, _, _, "bob", "secret2"));
                            cb.onAuth("bob", "secret2");
                            return nullptr;
                          }));

                  // A FURTHER held GET that must NOT be dispatched yet: when the second AUTH
                  // re-enters Pending, resumeAuthHeldRequests must stop. Times(0) on its
                  // splitter expectation pins the early-out.
                  Common::Redis::RespValuePtr extra_get(new Common::Redis::RespValue());
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*extra_get), _, _, _)).Times(0);

                  decoder_callbacks_->onRespValue(std::move(held_auth));
                  decoder_callbacks_->onRespValue(std::move(extra_get));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Pending,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
  // Connection close at end of test: second-auth round trip is still in flight so
  // ProxyFilter::onEvent's external-auth branch invokes auth_client_->cancel() before
  // draining pending_requests_ (which still contains the in-flight AUTH PR and the held GET
  // PR). Drain satisfies the ~ProxyFilter() assertion.
  EXPECT_CALL(*external_auth_client_, cancel());
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// HELLO N AUTH ... succeeded + expiration set: the existing connectionAllowed expiration
// check applies to the HELLO AUTH path too. After the deferred HELLO Map is emitted the
// connection is allowed; advancing past expiration revokes it. Pins that the auth-expiration
// machinery is shared between the AUTH-command and HELLO-AUTH paths.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, HelloAuthExternalAuthExpirationStillTracked) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Pending,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        EXPECT_TRUE(filter_->connectionAllowed());
        time_source_.advanceTimeWait(std::chrono::hours(2));
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
