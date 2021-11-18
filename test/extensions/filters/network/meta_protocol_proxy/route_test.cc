#include "source/extensions/filters/network/meta_protocol_proxy/route.h"

#include "test/extensions/filters/network/meta_protocol_proxy/fake_codec.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/stream_filter.h"

#include "test/test_common/utility.h"
#include "test/test_common/registry.h"
#include "test/mocks/server/factory_context.h"

#include "source/common/config/metadata.h"

#include "gtest/gtest.h"
#include <memory>

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace {

class RouteEntryImplTest : public testing::Test {
public:
  class RouteConfig : public RouteSpecificFilterConfig {};

  void initialize(const std::string& yaml_config) {
    Registry::InjectFactory<NamedFilterConfigFactory> registration(filter_config_);
    ON_CALL(filter_config_, createRouteSpecificFilterConfig(_, _, _))
        .WillByDefault(
            Invoke([this](const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
                          ProtobufMessage::ValidationVisitor&) {
              auto route_config = std::make_shared<RouteConfig>();
              route_config_map_.emplace(filter_config_.name(), route_config);
              return route_config;
            }));

    ProtoRouteAction proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    route_ = std::make_shared<RouteEntryImpl>(proto_config, server_context_);
  }

protected:
  NiceMock<MockStreamFilterConfig> filter_config_;

  absl::flat_hash_map<std::string, RouteSpecificFilterConfigConstSharedPtr> route_config_map_;

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  RouteEntryConstSharedPtr route_;
};

TEST_F(RouteEntryImplTest, SimpleClusterName) {
  const std::string yaml_config = R"EOF(
    cluster: cluster_0
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->clusterName(), "cluster_0");
};

TEST_F(RouteEntryImplTest, DefaultTimeout) {
  const std::string yaml_config = R"EOF(
    cluster: cluster_0
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->timeout().count(), 15000);
};

TEST_F(RouteEntryImplTest, RouteMetadata) {
  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    metadata:
      filter_metadata:
        mock_filter:
          key_0: value_0
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(
      "value_0",
      Config::Metadata::metadataValue(&route_->metadata(), "mock_filter", "key_0").string_value());
};

TEST_F(RouteEntryImplTest, RoutePerFilterConfig) {
  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    per_filter_config:
      envoy.filters.meta_protocol.mock_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value: { "key_0": "value_0" }
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->perFilterConfig("envoy.filters.meta_protocol.mock_filter"),
            route_config_map_.at("envoy.filters.meta_protocol.mock_filter").get());
};

} // namespace
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
