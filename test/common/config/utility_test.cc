#include "common/config/cds_json.h"
#include "common/config/lds_json.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/local_info/mocks.h"
#include "test/test_common/utility.h"

#include "api/eds.pb.h"
#include "fmt/format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
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
  EXPECT_EQ("test_rest_legacy_cluster", api_config_source_rest_legacy.cluster_name(0));

  envoy::api::v2::ApiConfigSource api_config_source_rest;
  Utility::translateApiConfigSource("test_rest_cluster", 20000, ApiType::get().Rest,
                                    api_config_source_rest);
  EXPECT_EQ(envoy::api::v2::ApiConfigSource::REST, api_config_source_rest.api_type());
  EXPECT_EQ(20000, Protobuf::util::TimeUtil::DurationToMilliseconds(
                       api_config_source_rest.refresh_delay()));
  EXPECT_EQ("test_rest_cluster", api_config_source_rest.cluster_name(0));

  envoy::api::v2::ApiConfigSource api_config_source_grpc;
  Utility::translateApiConfigSource("test_grpc_cluster", 30000, ApiType::get().Grpc,
                                    api_config_source_grpc);
  EXPECT_EQ(envoy::api::v2::ApiConfigSource::GRPC, api_config_source_grpc.api_type());
  EXPECT_EQ(30000, Protobuf::util::TimeUtil::DurationToMilliseconds(
                       api_config_source_grpc.refresh_delay()));
  EXPECT_EQ("test_grpc_cluster", api_config_source_grpc.cluster_name(0));
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
    envoy::api::v2::VirtualHost vhost;
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
    envoy::api::v2::filter::Rds rds;
    EXPECT_THROW_WITH_MESSAGE(Config::Utility::translateRdsConfig(*json_object_ptr, rds),
                              EnvoyException, err_prefix + err_suffix);
  }
}

} // namespace Config
} // namespace Envoy
