#include "envoy/api/v2/eds.pb.h"
#include "envoy/common/exception.h"
#include "envoy/stats/stats.h"

#include "common/common/fmt.h"
#include "common/config/cds_json.h"
#include "common/config/lds_json.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Config {

TEST(UtilityTest, GetTypedResources) {
  envoy::api::v2::DiscoveryResponse response;
  EXPECT_EQ(0, Utility::getTypedResources<envoy::api::v2::ClusterLoadAssignment>(response).size());

  envoy::api::v2::ClusterLoadAssignment load_assignment_0;
  load_assignment_0.set_cluster_name("0");
  response.add_resources()->PackFrom(load_assignment_0);
  envoy::api::v2::ClusterLoadAssignment load_assignment_1;
  load_assignment_1.set_cluster_name("1");
  response.add_resources()->PackFrom(load_assignment_1);

  auto typed_resources =
      Utility::getTypedResources<envoy::api::v2::ClusterLoadAssignment>(response);
  EXPECT_EQ(2, typed_resources.size());
  EXPECT_EQ("0", typed_resources[0].cluster_name());
  EXPECT_EQ("1", typed_resources[1].cluster_name());
}

TEST(UtilityTest, ComputeHashedVersion) {
  EXPECT_EQ("hash_2e1472b57af294d1", Utility::computeHashedVersion("{}").first);
  EXPECT_EQ("hash_33bf00a859c4ba3f", Utility::computeHashedVersion("foo").first);
}

TEST(UtilityTest, ApiConfigSourceRefreshDelay) {
  envoy::api::v2::core::ApiConfigSource api_config_source;
  api_config_source.mutable_refresh_delay()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(1234));
  EXPECT_EQ(1234, Utility::apiConfigSourceRefreshDelay(api_config_source).count());
}

TEST(UtilityTest, ApiConfigSourceDefaultRequestTimeout) {
  envoy::api::v2::core::ApiConfigSource api_config_source;
  EXPECT_EQ(1000, Utility::apiConfigSourceRequestTimeout(api_config_source).count());
}

TEST(UtilityTest, ApiConfigSourceRequestTimeout) {
  envoy::api::v2::core::ApiConfigSource api_config_source;
  api_config_source.mutable_request_timeout()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(1234));
  EXPECT_EQ(1234, Utility::apiConfigSourceRequestTimeout(api_config_source).count());
}

TEST(UtilityTest, TranslateApiConfigSource) {
  envoy::api::v2::core::ApiConfigSource api_config_source_rest_legacy;
  Utility::translateApiConfigSource("test_rest_legacy_cluster", 10000, ApiType::get().RestLegacy,
                                    api_config_source_rest_legacy);
  EXPECT_EQ(envoy::api::v2::core::ApiConfigSource::REST_LEGACY,
            api_config_source_rest_legacy.api_type());
  EXPECT_EQ(10000,
            DurationUtil::durationToMilliseconds(api_config_source_rest_legacy.refresh_delay()));
  EXPECT_EQ("test_rest_legacy_cluster", api_config_source_rest_legacy.cluster_names(0));

  envoy::api::v2::core::ApiConfigSource api_config_source_rest;
  Utility::translateApiConfigSource("test_rest_cluster", 20000, ApiType::get().Rest,
                                    api_config_source_rest);
  EXPECT_EQ(envoy::api::v2::core::ApiConfigSource::REST, api_config_source_rest.api_type());
  EXPECT_EQ(20000, DurationUtil::durationToMilliseconds(api_config_source_rest.refresh_delay()));
  EXPECT_EQ("test_rest_cluster", api_config_source_rest.cluster_names(0));

  envoy::api::v2::core::ApiConfigSource api_config_source_grpc;
  Utility::translateApiConfigSource("test_grpc_cluster", 30000, ApiType::get().Grpc,
                                    api_config_source_grpc);
  EXPECT_EQ(envoy::api::v2::core::ApiConfigSource::GRPC, api_config_source_grpc.api_type());
  EXPECT_EQ(30000, DurationUtil::durationToMilliseconds(api_config_source_grpc.refresh_delay()));
  EXPECT_EQ("test_grpc_cluster",
            api_config_source_grpc.grpc_services(0).envoy_grpc().cluster_name());
}

TEST(UtilityTest, createTagProducer) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  auto producer = Utility::createTagProducer(bootstrap);
  ASSERT(producer != nullptr);
  std::vector<Stats::Tag> tags;
  auto extracted_name = producer->produceTags("http.config_test.rq_total", tags);
  ASSERT_EQ(extracted_name, "http.rq_total");
  ASSERT_EQ(tags.size(), 1);
}

TEST(UtilityTest, ObjNameLength) {
  Stats::StatsOptionsImpl stats_options;
  std::string name = "listenerwithareallyreallyreallyreallyreallyreallyreallyreallyreallyreallyreal"
                     "lyreallyreallyreallyreallyreallylongnamemorethanmaxcharsallowedbyschema";
  std::string err_prefix;
  std::string err_suffix = fmt::format(": Length of {} ({}) exceeds allowed maximum length ({})",
                                       name, name.length(), stats_options.maxNameLength());
  {
    err_prefix = "test";
    EXPECT_THROW_WITH_MESSAGE(Utility::checkObjNameLength(err_prefix, name, stats_options),
                              EnvoyException, err_prefix + err_suffix);
  }

  {
    err_prefix = "Invalid listener name";
    std::string json =
        R"EOF({ "name": ")EOF" + name + R"EOF(", "address": "foo", "filters":[]})EOF";
    auto json_object_ptr = Json::Factory::loadFromString(json);

    envoy::api::v2::Listener listener;
    EXPECT_THROW_WITH_MESSAGE(
        Config::LdsJson::translateListener(*json_object_ptr, listener, stats_options),
        EnvoyException, err_prefix + err_suffix);
  }

  {
    err_prefix = "Invalid virtual host name";
    std::string json = R"EOF({ "name": ")EOF" + name + R"EOF(", "domains": [], "routes": []})EOF";
    auto json_object_ptr = Json::Factory::loadFromString(json);
    envoy::api::v2::route::VirtualHost vhost;
    EXPECT_THROW_WITH_MESSAGE(
        Config::RdsJson::translateVirtualHost(*json_object_ptr, vhost, stats_options),
        EnvoyException, err_prefix + err_suffix);
  }

  {
    err_prefix = "Invalid cluster name";
    std::string json =
        R"EOF({ "name": ")EOF" + name +
        R"EOF(", "type": "static", "lb_type": "random", "connect_timeout_ms" : 1})EOF";
    auto json_object_ptr = Json::Factory::loadFromString(json);
    envoy::api::v2::Cluster cluster;
    envoy::api::v2::core::ConfigSource eds_config;
    EXPECT_THROW_WITH_MESSAGE(
        Config::CdsJson::translateCluster(*json_object_ptr, eds_config, cluster, stats_options),
        EnvoyException, err_prefix + err_suffix);
  }

  {
    err_prefix = "Invalid route_config name";
    std::string json = R"EOF({ "route_config_name": ")EOF" + name + R"EOF(", "cluster": "foo"})EOF";
    auto json_object_ptr = Json::Factory::loadFromString(json);
    envoy::config::filter::network::http_connection_manager::v2::Rds rds;
    EXPECT_THROW_WITH_MESSAGE(
        Config::Utility::translateRdsConfig(*json_object_ptr, rds, stats_options), EnvoyException,
        err_prefix + err_suffix);
  }
}

TEST(UtilityTest, UnixClusterDns) {

  std::string cluster_type;
  cluster_type = "strict_dns";
  std::string json =
      R"EOF({ "name": "test", "type": ")EOF" + cluster_type +
      R"EOF(", "lb_type": "random", "connect_timeout_ms" : 1, "hosts": [{"url": "unix:///test.sock"}]})EOF";
  auto json_object_ptr = Json::Factory::loadFromString(json);
  envoy::api::v2::Cluster cluster;
  envoy::api::v2::core::ConfigSource eds_config;
  Stats::StatsOptionsImpl stats_options;
  EXPECT_THROW_WITH_MESSAGE(
      Config::CdsJson::translateCluster(*json_object_ptr, eds_config, cluster, stats_options),
      EnvoyException, "unresolved URL must be TCP scheme, got: unix:///test.sock");
}

TEST(UtilityTest, UnixClusterStatic) {

  std::string cluster_type;
  cluster_type = "static";
  std::string json =
      R"EOF({ "name": "test", "type": ")EOF" + cluster_type +
      R"EOF(", "lb_type": "random", "connect_timeout_ms" : 1, "hosts": [{"url": "unix:///test.sock"}]})EOF";
  auto json_object_ptr = Json::Factory::loadFromString(json);
  envoy::api::v2::Cluster cluster;
  envoy::api::v2::core::ConfigSource eds_config;
  Stats::StatsOptionsImpl stats_options;
  Config::CdsJson::translateCluster(*json_object_ptr, eds_config, cluster, stats_options);
  EXPECT_EQ("/test.sock", cluster.hosts(0).pipe().path());
}

TEST(UtilityTest, CheckFilesystemSubscriptionBackingPath) {
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkFilesystemSubscriptionBackingPath("foo"), EnvoyException,
      "envoy::api::v2::Path must refer to an existing path in the system: 'foo' does not exist");
  std::string test_path = TestEnvironment::temporaryDirectory();
  Utility::checkFilesystemSubscriptionBackingPath(test_path);
}

// TEST(UtilityTest, FactoryForGrpcApiConfigSource) should catch misconfigured
// API configs along the dimension of ApiConfigSource type.
TEST(UtilityTest, FactoryForGrpcApiConfigSource) {
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager;
  Stats::MockStore scope;

  {
    envoy::api::v2::core::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope),
        EnvoyException, "API configs must have either a gRPC service or a cluster name defined");
  }

  {
    envoy::api::v2::core::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services();
    api_config_source.add_grpc_services();
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope),
        EnvoyException,
        "envoy::api::v2::core::ConfigSource::GRPC must have a single gRPC service specified");
  }

  {
    envoy::api::v2::core::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
    api_config_source.add_cluster_names();
    // this also logs a warning for setting REST cluster names for a gRPC API config.
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope),
        EnvoyException,
        "envoy::api::v2::core::ConfigSource::GRPC must not have a cluster name specified.");
  }

  {
    envoy::api::v2::core::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
    api_config_source.add_cluster_names();
    api_config_source.add_cluster_names();
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope),
        EnvoyException,
        "envoy::api::v2::core::ConfigSource::GRPC must not have a cluster name specified.");
  }

  {
    envoy::api::v2::core::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    // this also logs a warning for configuring gRPC clusters for a REST API config.
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope),
        EnvoyException,
        "envoy::api::v2::core::ConfigSource, if not of type gRPC, must not have a gRPC service "
        "specified");
  }

  {
    envoy::api::v2::core::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    envoy::api::v2::core::GrpcService expected_grpc_service;
    expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(expected_grpc_service), Ref(scope), _));
    Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope);
  }

  {
    envoy::api::v2::core::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(api_config_source.grpc_services(0)), Ref(scope), _));
    Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope);
  }
}

TEST(CheckApiConfigSourceSubscriptionBackingClusterTest, GrpcClusterTestAcrossTypes) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  Upstream::ClusterManager::ClusterInfoMap cluster_map;

  // API of type GRPC
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);

  // GRPC cluster without GRPC services.
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException, "API configs must have either a gRPC service or a cluster name defined");

  // Non-existent cluster.
  api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo_cluster");
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS cluster: "
      "'foo_cluster' does not exist, was added via api, or is an EDS cluster");

  // Dynamic Cluster.
  Upstream::MockCluster cluster;
  cluster_map.emplace("foo_cluster", cluster);
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS cluster: "
      "'foo_cluster' does not exist, was added via api, or is an EDS cluster");

  // EDS Cluster backing EDS Cluster.
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type()).WillOnce(Return(envoy::api::v2::Cluster::EDS));
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS cluster: "
      "'foo_cluster' does not exist, was added via api, or is an EDS cluster");

  // All ok.
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type());
  Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source);

  // API with cluster_names set should be rejected.
  api_config_source->add_cluster_names("foo_cluster");
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource::GRPC must not have a cluster name specified.");
}

TEST(CheckApiConfigSourceSubscriptionBackingClusterTest, RestClusterTestAcrossTypes) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);

  // Non-existent cluster.
  api_config_source->add_cluster_names("foo_cluster");
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS cluster: "
      "'foo_cluster' does not exist, was added via api, or is an EDS cluster");

  // Dynamic Cluster.
  Upstream::MockCluster cluster;
  cluster_map.emplace("foo_cluster", cluster);
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS cluster: "
      "'foo_cluster' does not exist, was added via api, or is an EDS cluster");

  // EDS Cluster backing EDS Cluster.
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type()).WillOnce(Return(envoy::api::v2::Cluster::EDS));
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS cluster: "
      "'foo_cluster' does not exist, was added via api, or is an EDS cluster");

  // All ok.
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type());
  Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source);
}

} // namespace Config
} // namespace Envoy
