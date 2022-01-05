#include <memory>

#include "source/common/config/metadata.h"
#include "source/extensions/filters/network/meta_protocol_proxy/match.h"
#include "source/extensions/filters/network/meta_protocol_proxy/route.h"

#include "test/extensions/filters/network/meta_protocol_proxy/fake_codec.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/filter.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/route.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

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

/**
 * Test the method that get cluster name from route entry.
 */
TEST_F(RouteEntryImplTest, SimpleClusterName) {
  const std::string yaml_config = R"EOF(
    cluster: cluster_0
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->clusterName(), "cluster_0");
};

/**
 * Test the method that get filter metadata from the route entry.
 */
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

/**
 * Test the method that get route level per filter config from the route entry. This test also
 * verifies that the proto per filter config can be loaded correctly.
 */
TEST_F(RouteEntryImplTest, RoutePerFilterConfig) {
  Registry::InjectFactory<NamedFilterConfigFactory> registration(filter_config_);
  ON_CALL(filter_config_, createRouteSpecificFilterConfig(_, _, _))
      .WillByDefault(
          Invoke([this](const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
                        ProtobufMessage::ValidationVisitor&) {
            auto route_config = std::make_shared<RouteConfig>();
            route_config_map_.emplace(filter_config_.name(), route_config);
            return route_config;
          }));

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

/**
 * Test the simple route action wrapper.
 */
TEST(RouteMatchActionTest, SimpleRouteMatchActionTest) {
  auto entry = std::make_shared<NiceMock<MockRouteEntry>>();
  RouteMatchAction action(entry);

  EXPECT_EQ(action.route().get(), entry.get());
}

/**
 * Test the simple data input validator.
 */
TEST(RouteActionValidationVisitorTest, SimpleRouteActionValidationVisitorTest) {
  RouteActionValidationVisitor visitor;
  ServiceMatchDataInputFactory factory;

  EXPECT_EQ(visitor.performDataInputValidation(factory, ""), absl::OkStatus());
}

/**
 * Test the route match action factory.
 */
TEST(RouteMatchActionFactoryTest, SimpleRouteMatchActionFactoryTest) {
  RouteMatchActionFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  EXPECT_EQ("envoy.matching.action.meta_protocol.route", factory.name());

  EXPECT_EQ(factory.createEmptyConfigProto()->GetTypeName(), ProtoRouteAction().GetTypeName());

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    metadata:
      filter_metadata:
        mock_filter:
          key_0: value_0
  )EOF";
  ProtoRouteAction proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);
  RouteActionContext context{server_context};

  auto factory_cb = factory.createActionFactoryCb(proto_config, context,
                                                  server_context.messageValidationVisitor());

  EXPECT_EQ(factory_cb()->getTyped<RouteMatchAction>().route().get(),
            factory_cb()->getTyped<RouteMatchAction>().route().get());

  EXPECT_EQ(factory_cb()->getTyped<RouteMatchAction>().route()->clusterName(), "cluster_0");
}

class RouteMatcherImplTest : public testing::Test {
public:
  void initialize(const std::string& yaml_config) {
    ProtoRouteConfiguration proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);
    route_matcher_ = std::make_unique<RouteMatcherImpl>(proto_config, factory_context_);
  }

protected:
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;

  std::unique_ptr<RouteMatcherImpl> route_matcher_;
};

static const std::string RouteConfigurationYaml = R"EOF(
name: test_matcher_tree
routes:
  matcher_list:
    matchers:
    - predicate:
        and_matcher:
          predicate:
          - single_predicate:
              input:
                name: envoy.matching.meta_protocol.input.service
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.ServiceMatchInput
              value_match:
                exact: "service_0"
          - single_predicate:
              input:
                name: envoy.matching.meta_protocol.input.method
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.MethodMatchInput
              value_match:
                exact: "method_0"
          - or_matcher:
              predicate:
              - single_predicate:
                  input:
                    name: envoy.matching.meta_protocol.input.property
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.PropertyMatchInput
                      property_name: "key_0"
                  value_match:
                    exact: "value_0"
              - single_predicate:
                  input:
                    name: envoy.matching.meta_protocol.input.property
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.PropertyMatchInput
                      property_name: "key_1"
                  value_match:
                    exact: "value_1"
      on_match:
        action:
          name: envoy.matching.action.meta_protocol.route
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.action.v3.RouteAction
            cluster: "cluster_0"
            metadata:
              filter_metadata:
                mock_filter:
                  key_0: value_0
)EOF";

/**
 * Test the simple name method.
 */
TEST_F(RouteMatcherImplTest, SimpleNameMethod) {
  initialize(RouteConfigurationYaml);
  EXPECT_EQ(route_matcher_->name(), "test_matcher_tree");
}

/**
 * Test the case where the request matches a route entry in the matching tree.
 */
TEST_F(RouteMatcherImplTest, RouteMatch) {
  initialize(RouteConfigurationYaml);

  FakeStreamCodecFactory::FakeRequest fake_request_0;
  fake_request_0.host_ = "service_0";
  fake_request_0.method_ = "method_0";
  fake_request_0.data_.insert({"key_0", "value_0"});

  FakeStreamCodecFactory::FakeRequest fake_request_1;
  fake_request_1.host_ = "service_0";
  fake_request_1.method_ = "method_0";
  fake_request_1.data_.insert({"key_1", "value_1"});

  auto route_entry_0 = route_matcher_->routeEntry(fake_request_0);
  auto route_entry_1 = route_matcher_->routeEntry(fake_request_1);

  EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
  EXPECT_NE(route_entry_0.get(), nullptr);

  EXPECT_EQ(route_entry_0->clusterName(), "cluster_0");
}

/**
 * Test the case where the request not matches any route entry in the matching tree.
 */
TEST_F(RouteMatcherImplTest, RouteNotMatch) {
  initialize(RouteConfigurationYaml);

  // Test the service not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "service_x";
    fake_request.method_ = "method_0";
    fake_request.data_.insert({"key_0", "value_0"});

    EXPECT_EQ(nullptr, route_matcher_->routeEntry(fake_request));
  }

  // Test the method not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "service_0";
    fake_request.method_ = "method_x";
    fake_request.data_.insert({"key_0", "value_0"});

    EXPECT_EQ(nullptr, route_matcher_->routeEntry(fake_request));
  }

  // Test the headers not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "service_0";
    fake_request.method_ = "method_0";
    EXPECT_EQ(nullptr, route_matcher_->routeEntry(fake_request));
  }
}

static const std::string RouteConfigurationYamlWithUnknownInput = R"EOF(
name: test_matcher_tree
routes:
  matcher_list:
    matchers:
    - predicate:
        and_matcher:
          predicate:
          - single_predicate:
              input:
                name: envoy.matching.meta_protocol.input.unknown_input
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.v3.UnknownInput
              value_match:
                exact: "service_0"
      on_match:
        action:
          name: envoy.matching.action.meta_protocol.route
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.v3.RouteAction
            cluster: "cluster_0"
            metadata:
              filter_metadata:
                mock_filter:
                  key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationWithUnknownInput) {
  EXPECT_THROW(initialize(RouteConfigurationYamlWithUnknownInput), EnvoyException);
  EXPECT_EQ(nullptr, route_matcher_.get());
}

} // namespace
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
