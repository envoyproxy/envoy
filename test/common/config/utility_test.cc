#include "envoy/api/v2/eds.pb.h"
#include "envoy/common/exception.h"

#include "common/common/fmt.h"
#include "common/config/cds_json.h"
#include "common/config/lds_json.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/stats_impl.h"

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
  envoy::api::v2::ApiConfigSource api_config_source;
  api_config_source.mutable_refresh_delay()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(1234));
  EXPECT_EQ(1234, Utility::apiConfigSourceRefreshDelay(api_config_source).count());
}

TEST(UtilityTest, TranslateApiConfigSource) {
  envoy::api::v2::ApiConfigSource api_config_source_rest_legacy;
  Utility::translateApiConfigSource("test_rest_legacy_cluster", 10000, ApiType::get().RestLegacy,
                                    api_config_source_rest_legacy);
  EXPECT_EQ(envoy::api::v2::ApiConfigSource::REST_LEGACY, api_config_source_rest_legacy.api_type());
  EXPECT_EQ(10000, Protobuf::util::TimeUtil::DurationToMilliseconds(
                       api_config_source_rest_legacy.refresh_delay()));
  EXPECT_EQ("test_rest_legacy_cluster", api_config_source_rest_legacy.cluster_names(0));

  envoy::api::v2::ApiConfigSource api_config_source_rest;
  Utility::translateApiConfigSource("test_rest_cluster", 20000, ApiType::get().Rest,
                                    api_config_source_rest);
  EXPECT_EQ(envoy::api::v2::ApiConfigSource::REST, api_config_source_rest.api_type());
  EXPECT_EQ(20000, Protobuf::util::TimeUtil::DurationToMilliseconds(
                       api_config_source_rest.refresh_delay()));
  EXPECT_EQ("test_rest_cluster", api_config_source_rest.cluster_names(0));

  envoy::api::v2::ApiConfigSource api_config_source_grpc;
  Utility::translateApiConfigSource("test_grpc_cluster", 30000, ApiType::get().Grpc,
                                    api_config_source_grpc);
  EXPECT_EQ(envoy::api::v2::ApiConfigSource::GRPC, api_config_source_grpc.api_type());
  EXPECT_EQ(30000, Protobuf::util::TimeUtil::DurationToMilliseconds(
                       api_config_source_grpc.refresh_delay()));
  EXPECT_EQ("test_grpc_cluster", api_config_source_grpc.cluster_names(0));
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

  std::string name = "listenerwithareallyreallylongnamemorethanmaxcharsallowedbyschema";
  std::string err_prefix;
  std::string err_suffix = fmt::format(": Length of {} ({}) exceeds allowed maximum length ({})",
                                       name, name.length(), Stats::RawStatData::maxObjNameLength());
  {
    err_prefix = "test";
    EXPECT_THROW_WITH_MESSAGE(Utility::checkObjNameLength(err_prefix, name), EnvoyException,
                              err_prefix + err_suffix);
  }

  {
    err_prefix = "Invalid listener name";
    std::string json =
        R"EOF({ "name": ")EOF" + name + R"EOF(", "address": "foo", "filters":[]})EOF";
    auto json_object_ptr = Json::Factory::loadFromString(json);

    envoy::api::v2::Listener listener;
    EXPECT_THROW_WITH_MESSAGE(Config::LdsJson::translateListener(*json_object_ptr, listener),
                              EnvoyException, err_prefix + err_suffix);
  }

  {
    err_prefix = "Invalid virtual host name";
    std::string json = R"EOF({ "name": ")EOF" + name + R"EOF(", "domains": [], "routes": []})EOF";
    auto json_object_ptr = Json::Factory::loadFromString(json);
    envoy::api::v2::route::VirtualHost vhost;
    EXPECT_THROW_WITH_MESSAGE(Config::RdsJson::translateVirtualHost(*json_object_ptr, vhost),
                              EnvoyException, err_prefix + err_suffix);
  }

  {
    err_prefix = "Invalid cluster name";
    std::string json =
        R"EOF({ "name": ")EOF" + name +
        R"EOF(", "type": "static", "lb_type": "random", "connect_timeout_ms" : 1})EOF";
    auto json_object_ptr = Json::Factory::loadFromString(json);
    envoy::api::v2::Cluster cluster;
    envoy::api::v2::ConfigSource eds_config;
    EXPECT_THROW_WITH_MESSAGE(
        Config::CdsJson::translateCluster(*json_object_ptr, eds_config, cluster), EnvoyException,
        err_prefix + err_suffix);
  }

  {
    err_prefix = "Invalid route_config name";
    std::string json = R"EOF({ "route_config_name": ")EOF" + name + R"EOF(", "cluster": "foo"})EOF";
    auto json_object_ptr = Json::Factory::loadFromString(json);
    envoy::api::v2::filter::network::Rds rds;
    EXPECT_THROW_WITH_MESSAGE(Config::Utility::translateRdsConfig(*json_object_ptr, rds),
                              EnvoyException, err_prefix + err_suffix);
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
  envoy::api::v2::ConfigSource eds_config;
  EXPECT_THROW_WITH_MESSAGE(
      Config::CdsJson::translateCluster(*json_object_ptr, eds_config, cluster), EnvoyException,
      "unresolved URL must be TCP scheme, got: unix:///test.sock");
}

TEST(UtilityTest, UnixClusterStatic) {

  std::string cluster_type;
  cluster_type = "static";
  std::string json =
      R"EOF({ "name": "test", "type": ")EOF" + cluster_type +
      R"EOF(", "lb_type": "random", "connect_timeout_ms" : 1, "hosts": [{"url": "unix:///test.sock"}]})EOF";
  auto json_object_ptr = Json::Factory::loadFromString(json);
  envoy::api::v2::Cluster cluster;
  envoy::api::v2::ConfigSource eds_config;
  Config::CdsJson::translateCluster(*json_object_ptr, eds_config, cluster);
  EXPECT_EQ("/test.sock", cluster.hosts(0).pipe().path());
}

TEST(UtilityTest, CheckFilesystemSubscriptionBackingPath) {
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkFilesystemSubscriptionBackingPath("foo"), EnvoyException,
      "envoy::api::v2::Path must refer to an existing path in the system: 'foo' does not exist");
  std::string test_path = TestEnvironment::temporaryDirectory();
  Utility::checkFilesystemSubscriptionBackingPath(test_path);
}

TEST(UtilityTest, CheckApiConfigSourceSubscriptionBackingCluster) {
  envoy::api::v2::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->add_cluster_names("foo_cluster");
  Upstream::ClusterManager::ClusterInfoMap cluster_map;

  // Non-existent cluster.
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::ConfigSource must have a statically defined non-EDS cluster: 'foo_cluster' "
      "does not exist, was added via api, or is an EDS cluster");

  // Dynamic Cluster.
  Upstream::MockCluster cluster;
  cluster_map.emplace("foo_cluster", cluster);
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::ConfigSource must have a statically defined non-EDS cluster: 'foo_cluster' "
      "does not exist, was added via api, or is an EDS cluster");

  // EDS Cluster backing EDS Cluster.
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type()).WillOnce(Return(envoy::api::v2::Cluster::EDS));
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source),
      EnvoyException,
      "envoy::api::v2::ConfigSource must have a statically defined non-EDS cluster: 'foo_cluster' "
      "does not exist, was added via api, or is an EDS cluster");

  // All ok.
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type());
  Utility::checkApiConfigSourceSubscriptionBackingCluster(cluster_map, *api_config_source);
}

TEST(UtilityTest, FactoryForApiConfigSource) {
  Grpc::MockAsyncClientManager async_client_manager;
  Stats::MockStore scope;

  {
    envoy::api::v2::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::GRPC);
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForApiConfigSource(async_client_manager, api_config_source, scope),
        EnvoyException, "Missing gRPC services in envoy::api::v2::ApiConfigSource:");
  }

  {
    envoy::api::v2::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services();
    api_config_source.add_grpc_services();
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForApiConfigSource(async_client_manager, api_config_source, scope),
        EnvoyException,
        "Only singleton gRPC service lists supported in envoy::api::v2::ApiConfigSource:");
  }

  {
    envoy::api::v2::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::GRPC);
    api_config_source.add_cluster_names();
    api_config_source.add_cluster_names();
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForApiConfigSource(async_client_manager, api_config_source, scope),
        EnvoyException,
        "Only singleton cluster name lists supported in envoy::api::v2::ApiConfigSource:");
  }

  {
    envoy::api::v2::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::GRPC);
    api_config_source.add_cluster_names("foo");
    envoy::api::v2::GrpcService expected_grpc_service;
    expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(expected_grpc_service), Ref(scope)));
    Utility::factoryForApiConfigSource(async_client_manager, api_config_source, scope);
  }

  {
    envoy::api::v2::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(api_config_source.grpc_services(0)), Ref(scope)));
    Utility::factoryForApiConfigSource(async_client_manager, api_config_source, scope);
  }
}

} // namespace Config
} // namespace Envoy
