#include <memory>

#include "source/common/config/metadata.h"
#include "source/extensions/filters/network/generic_proxy/match.h"
#include "source/extensions/filters/network/generic_proxy/route_impl.h"

#include "test/extensions/filters/network/generic_proxy/fake_codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/filter.h"
#include "test/extensions/filters/network/generic_proxy/mocks/route.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
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
 * Test the method that get route name and cluster name from route entry.
 */
TEST_F(RouteEntryImplTest, SimpleRouteNameAndClusterName) {
  const std::string yaml_config = R"EOF(
    name: route_0
    cluster: cluster_0
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->name(), "route_0");
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

struct Foo : public Envoy::Config::TypedMetadata::Object {};
struct Baz : public Envoy::Config::TypedMetadata::Object {
  Baz(std::string n) : name(n) {}
  std::string name;
};
class BazFactory : public RouteTypedMetadataFactory {
public:
  std::string name() const override { return "baz"; }
  // Returns nullptr (conversion failure) if d is empty.
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const ProtobufWkt::Struct& d) const override {
    if (d.fields().find("name") != d.fields().end()) {
      return std::make_unique<Baz>(d.fields().at("name").string_value());
    }
    throw EnvoyException("Cannot create a Baz when metadata is empty.");
  }

  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const ProtobufWkt::Any&) const override {
    return nullptr;
  }
};

/**
 * Test the method that get filter metadata from the route entry.
 */
TEST_F(RouteEntryImplTest, RouteTypedMetadata) {
  BazFactory baz_factory;
  Registry::InjectFactory<RouteTypedMetadataFactory> registered_factory(baz_factory);

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    metadata:
      filter_metadata:
        foo:
          key_0: value_0
        baz:
          name: baz_name
  )EOF";
  initialize(yaml_config);

  // Only get valid value when type and name are matched.
  EXPECT_EQ("baz_name", route_->typedMetadata().get<Baz>(baz_factory.name())->name);

  EXPECT_EQ(nullptr, route_->typedMetadata().get<Foo>(baz_factory.name()));
  EXPECT_EQ(nullptr, route_->typedMetadata().get<Baz>("foo"));
  EXPECT_EQ(nullptr, route_->typedMetadata().get<Foo>("foo"));
};

/**
 * Test the method that get route level per filter config from the route entry. This test also
 * verifies that the proto per filter config can be loaded correctly.
 */
TEST_F(RouteEntryImplTest, RoutePerFilterConfig) {
  ON_CALL(filter_config_, createEmptyRouteConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
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
      envoy.filters.generic.mock_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value: { "key_0": "value_0" }
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->perFilterConfig("envoy.filters.generic.mock_filter"),
            route_config_map_.at("envoy.filters.generic.mock_filter").get());
};

/**
 * Test the method that get timeout from the route entry.
 */
TEST_F(RouteEntryImplTest, RouteTimeout) {
  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    timeout: 10s
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(10000, route_->timeout().count());
};

/**
 * Test the method that get route level per filter config from the route entry. In this case,
 * unexpected type is used to find the filter factory and finally an exception is thrown.
 */
TEST_F(RouteEntryImplTest, RoutePerFilterConfigWithUnknownType) {
  ON_CALL(filter_config_, createEmptyRouteConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
  Registry::InjectFactory<NamedFilterConfigFactory> registration(filter_config_);

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    per_filter_config:
      envoy.filters.generic.mock_filter:
        # The mock filter is registered with the type of google.protobuf.Struct.
        # So the google.protobuf.Value cannot be used to find the mock filter.
        "@type": type.googleapis.com/google.protobuf.Value
        value: { "key_0": "value_0" }
  )EOF";

  // The configuration will be rejected because the extension cannot be found.
  EXPECT_THROW_WITH_MESSAGE(
      { initialize(yaml_config); }, EnvoyException,
      "Didn't find a registered implementation for 'envoy.filters.generic.mock_filter' with type "
      "URL: 'google.protobuf.Value'");
}

/**
 * Test the method that get route level per filter config from the route entry. In this case,
 * unexpected type is used to find the filter factory. But the extension lookup by name is enabled
 * and the mock filter is found.
 */
TEST_F(RouteEntryImplTest, RoutePerFilterConfigWithUnknownTypeButEnableExtensionLookupByName) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});

  ON_CALL(filter_config_, createEmptyRouteConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
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
      envoy.filters.generic.mock_filter:
        # The mock filter is registered with the type of google.protobuf.Struct.
        # So the google.protobuf.Value cannot be used to find the mock filter.
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: type.googleapis.com/google.protobuf.Value
        value:
          value: { "key_0": "value_0" }
  )EOF";

  initialize(yaml_config);

  EXPECT_EQ(route_->perFilterConfig("envoy.filters.generic.mock_filter"),
            route_config_map_.at("envoy.filters.generic.mock_filter").get());
}

/**
 * Test the case where there is no route level proto available for the filter.
 */
TEST_F(RouteEntryImplTest, NullRouteEmptyProto) {
  ON_CALL(filter_config_, createEmptyRouteConfigProto()).WillByDefault(Invoke([]() {
    return nullptr;
  }));
  Registry::InjectFactory<NamedFilterConfigFactory> registration(filter_config_);

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    per_filter_config:
      envoy.filters.generic.mock_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value: { "key_0": "value_0" }
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->perFilterConfig("envoy.filters.generic.mock_filter"), nullptr);
};

/**
 * Test the case where there is no route level config available for the filter.
 */
TEST_F(RouteEntryImplTest, NullRouteSpecificConfig) {
  Registry::InjectFactory<NamedFilterConfigFactory> registration(filter_config_);
  ON_CALL(filter_config_, createEmptyRouteConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    per_filter_config:
      envoy.filters.generic.mock_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value: { "key_0": "value_0" }
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->perFilterConfig("envoy.filters.generic.mock_filter"), nullptr);
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

  EXPECT_EQ("envoy.matching.action.generic_proxy.route", factory.name());

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
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;

  std::unique_ptr<RouteMatcherImpl> route_matcher_;
};

TEST(NullRouteMatcherTest, NullRouteMatcherTest) {
  NullRouteMatcherImpl matcher;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  FakeStreamCodecFactory::FakeRequest fake_request_0;
  fake_request_0.host_ = "service_0";
  fake_request_0.method_ = "method_0";
  fake_request_0.data_.insert({"key_0", "value_0"});
  const MatchInput match_input_0(fake_request_0, stream_info, MatchAction::RouteAction);

  EXPECT_EQ(nullptr, matcher.routeEntry(match_input_0));
}

static const std::string RouteConfigurationYaml = R"EOF(
name: test_matcher_tree
virtual_hosts:
- name: service
  hosts:
  - service_0
  routes:
    matcher_list:
      matchers:
      - predicate:
          and_matcher:
            predicate:
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.host
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
                value_match:
                  exact: "service_0"
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.method
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.MethodMatchInput
                value_match:
                  exact: "method_0"
            - or_matcher:
                predicate:
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_0"
                    value_match:
                      exact: "value_0"
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_1"
                    value_match:
                      exact: "value_1"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    match_service: match_service
- name: prefix
  hosts:
  - "prefix*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          and_matcher:
            predicate:
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.host
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
                value_match:
                  exact: "prefix_service_0"
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.method
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.MethodMatchInput
                value_match:
                  exact: "method_0"
            - or_matcher:
                predicate:
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_0"
                    value_match:
                      exact: "value_0"
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_1"
                    value_match:
                      exact: "value_1"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_1"
              metadata:
                filter_metadata:
                  mock_filter:
                    match_prefix: match_prefix
- name: suffix
  hosts:
  - "*suffix"
  routes:
    matcher_list:
      matchers:
      - predicate:
          and_matcher:
            predicate:
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.host
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
                value_match:
                  exact: "service_0_suffix"
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.method
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.MethodMatchInput
                value_match:
                  exact: "method_0"
            - or_matcher:
                predicate:
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_0"
                    value_match:
                      exact: "value_0"
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_1"
                    value_match:
                      exact: "value_1"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_2"
              metadata:
                filter_metadata:
                  mock_filter:
                    match_suffix: match_suffix
- name: catch_all
  hosts:
  - "*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.property
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                property_name: "catch_all"
            value_match:
              exact: "catch_all"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_3"
              metadata:
                filter_metadata:
                  mock_filter:
                    catch_all: catch_all
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

  // Exact host searching.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;

    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "service_0";
    fake_request_0.method_ = "method_0";
    fake_request_0.data_.insert({"key_0", "value_0"});
    const MatchInput match_input_0(fake_request_0, stream_info, MatchAction::RouteAction);

    FakeStreamCodecFactory::FakeRequest fake_request_1;
    fake_request_1.host_ = "service_0";
    fake_request_1.method_ = "method_0";
    fake_request_1.data_.insert({"key_1", "value_1"});
    const MatchInput match_input_1(fake_request_1, stream_info, MatchAction::RouteAction);

    auto route_entry_0 = route_matcher_->routeEntry(match_input_0);
    auto route_entry_1 = route_matcher_->routeEntry(match_input_1);

    EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
    EXPECT_NE(route_entry_0.get(), nullptr);

    EXPECT_EQ(route_entry_0->clusterName(), "cluster_0");
  }

  // Prefix host searching.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;

    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "prefix_service_0";
    fake_request_0.method_ = "method_0";
    fake_request_0.data_.insert({"key_0", "value_0"});
    const MatchInput match_input_0(fake_request_0, stream_info, MatchAction::RouteAction);

    FakeStreamCodecFactory::FakeRequest fake_request_1;
    fake_request_1.host_ = "prefix_service_0";
    fake_request_1.method_ = "method_0";
    fake_request_1.data_.insert({"key_1", "value_1"});
    const MatchInput match_input_1(fake_request_1, stream_info, MatchAction::RouteAction);

    auto route_entry_0 = route_matcher_->routeEntry(match_input_0);
    auto route_entry_1 = route_matcher_->routeEntry(match_input_1);

    EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
    EXPECT_NE(route_entry_0.get(), nullptr);

    EXPECT_EQ(route_entry_0->clusterName(), "cluster_1");
  }

  // Suffix host searching but not match.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;

    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "suffix";
    fake_request_0.method_ = "method_0";
    const MatchInput match_input_0(fake_request_0, stream_info, MatchAction::RouteAction);

    EXPECT_EQ("catch_all", route_matcher_->findVirtualHost(match_input_0)->name());
    EXPECT_EQ(nullptr, route_matcher_->routeEntry(match_input_0));
  }

  // Suffix host searching.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;

    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "service_0_suffix";
    fake_request_0.method_ = "method_0";
    fake_request_0.data_.insert({"key_0", "value_0"});
    const MatchInput match_input_0(fake_request_0, stream_info, MatchAction::RouteAction);

    FakeStreamCodecFactory::FakeRequest fake_request_1;
    fake_request_1.host_ = "service_0_suffix";
    fake_request_1.method_ = "method_0";
    fake_request_1.data_.insert({"key_1", "value_1"});
    const MatchInput match_input_1(fake_request_1, stream_info, MatchAction::RouteAction);

    auto route_entry_0 = route_matcher_->routeEntry(match_input_0);
    auto route_entry_1 = route_matcher_->routeEntry(match_input_1);

    EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
    EXPECT_NE(route_entry_0.get(), nullptr);

    EXPECT_EQ(route_entry_0->clusterName(), "cluster_2");
  }

  // Catch all host.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;

    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "any_service";
    fake_request_0.method_ = "method_0";
    fake_request_0.data_.insert({"catch_all", "catch_all"});
    const MatchInput match_input_0(fake_request_0, stream_info, MatchAction::RouteAction);

    FakeStreamCodecFactory::FakeRequest fake_request_1;
    fake_request_1.host_ = "any_service";
    fake_request_1.method_ = "method_0";
    fake_request_1.data_.insert({"catch_all", "catch_all"});
    const MatchInput match_input_1(fake_request_1, stream_info, MatchAction::RouteAction);

    auto route_entry_0 = route_matcher_->routeEntry(match_input_0);
    auto route_entry_1 = route_matcher_->routeEntry(match_input_1);

    EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
    EXPECT_NE(route_entry_0.get(), nullptr);

    EXPECT_EQ(route_entry_0->clusterName(), "cluster_3");
  }
}

/**
 * Test the case where the request not matches any route entry in the matching tree.
 */
TEST_F(RouteMatcherImplTest, RouteNotMatch) {
  initialize(RouteConfigurationYaml);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  // Test the service not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "prefix_service_1";
    fake_request.method_ = "method_0";
    fake_request.data_.insert({"key_0", "value_0"});
    const MatchInput match_input(fake_request, stream_info, MatchAction::RouteAction);

    EXPECT_EQ("prefix", route_matcher_->findVirtualHost(match_input)->name());
    EXPECT_EQ(nullptr, route_matcher_->routeEntry(match_input));
  }

  // Test the method not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "service_0";
    fake_request.method_ = "method_x";
    fake_request.data_.insert({"key_0", "value_0"});
    const MatchInput match_input(fake_request, stream_info, MatchAction::RouteAction);

    EXPECT_EQ("service", route_matcher_->findVirtualHost(match_input)->name());
    EXPECT_EQ(nullptr, route_matcher_->routeEntry(match_input));
  }

  // Test the headers not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "service_0";
    fake_request.method_ = "method_0";
    const MatchInput match_input(fake_request, stream_info, MatchAction::RouteAction);

    EXPECT_EQ("service", route_matcher_->findVirtualHost(match_input)->name());
    EXPECT_EQ(nullptr, route_matcher_->routeEntry(match_input));
  }
}

static const std::string RouteConfigurationYamlWithUnknownInput = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.unknown_input
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.UnknownInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
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

static const std::string RouteConfigurationYamlWithoutDefaultHost = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "service_0"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, NoHostMatch) {
  initialize(RouteConfigurationYamlWithoutDefaultHost);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  // Test the host not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "any_service";
    fake_request.method_ = "method_0";
    fake_request.data_.insert({"key_0", "value_0"});
    const MatchInput match_input(fake_request, stream_info, MatchAction::RouteAction);

    EXPECT_EQ(nullptr, route_matcher_->routeEntry(match_input));
  }
}

static const std::string RouteConfigurationYamlWithRepeatedHost = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "service_0"
  - "service_0"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationYamlWithRepeatedHost) {
  EXPECT_THROW_WITH_MESSAGE(initialize(RouteConfigurationYamlWithRepeatedHost), EnvoyException,
                            "Only unique values for host are permitted. Duplicate "
                            "entry of domain service_0 in route test_matcher_tree");
  EXPECT_EQ(nullptr, route_matcher_.get());
}

static const std::string RouteConfigurationYamlWithMultipleWildcard = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "*"
  - "*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationYamlWithMultipleWildcard) {
  EXPECT_THROW_WITH_MESSAGE(
      initialize(RouteConfigurationYamlWithMultipleWildcard), EnvoyException,
      "Only a single wildcard domain is permitted in route test_matcher_tree");
  EXPECT_EQ(nullptr, route_matcher_.get());
}

static const std::string RouteConfigurationYamlWithMultipleWildcard2 = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
routes:
  matcher_list:
    matchers:
    - predicate:
        single_predicate:
          input:
            name: envoy.matching.generic_proxy.input.host
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
          value_match:
            exact: "service_0"
      on_match:
        action:
          name: envoy.matching.action.generic_proxy.route
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
            cluster: "cluster_0"
            metadata:
              filter_metadata:
                mock_filter:
                  key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationYamlWithMultipleWildcard2) {
  EXPECT_THROW_WITH_MESSAGE(initialize(RouteConfigurationYamlWithMultipleWildcard2), EnvoyException,
                            "'routes' cannot be specified at the same time as a "
                            "catch-all ('*') virtual host in route test_matcher_tree");
  EXPECT_EQ(nullptr, route_matcher_.get());
}

static const std::string RouteConfigurationYamlWithEmptyHost = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - ""
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationYamlWithEmptyHost) {
  EXPECT_THROW_WITH_MESSAGE(initialize(RouteConfigurationYamlWithEmptyHost), EnvoyException,
                            "Invalid empty host name in route test_matcher_tree");
  EXPECT_EQ(nullptr, route_matcher_.get());
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
