#include <cstddef>

#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/filter_chain/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {
namespace {

using testing::_;
using testing::ElementsAre;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

constexpr absl::string_view FilterConfigName = "envoy.filters.http.filter_chain";

// A mock filter factory that handles any filter configured with a `google.protobuf.Struct`
// typed config. Filters are resolved by their typed config type URL, so a single registered
// factory can back filters configured with arbitrary `name` fields. Each created filter adds a
// stream decoder filter and bumps `filter_added_`.
class MockFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  MockFilterConfigFactory() = default;

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return [this](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<Http::MockStreamDecoderFilter>());
      filter_added_++;
    };
  }

  Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContext(
      const Protobuf::Message&, const std::string&,
      Server::Configuration::ServerFactoryContext&) override {
    return [this](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<Http::MockStreamDecoderFilter>());
      filter_added_++;
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "mock_filter"; }

  size_t filter_added_{};
};

class FilterChainFactoryTest : public testing::Test {
public:
  void SetUp() override {
    mock_factory_ = std::make_shared<MockFilterConfigFactory>();
    registry_ = std::make_unique<
        Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory>>(
        *mock_factory_);

    // Record the order in which filters are applied to the chain. The production code calls
    // setFilterConfigName() with the configured filter name right before adding each filter.
    ON_CALL(callbacks_, setFilterConfigName(_))
        .WillByDefault(Invoke(
            [this](absl::string_view name) { applied_filters_.push_back(std::string(name)); }));
    ON_CALL(callbacks_, filterConfigName()).WillByDefault(Return(FilterConfigName));
  }

  void initialize(const std::string& yaml_config) {
    FilterChainConfigProto proto_config;
    if (!yaml_config.empty()) {
      TestUtility::loadFromYaml(yaml_config, proto_config);
    }
    auto callback_or_error = factory_.createFilterFactoryFromProto(proto_config, "test.", context_);
    EXPECT_TRUE(callback_or_error.status().ok());
    cb_ = callback_or_error.value();
  }

  // Create a per-route filter chain config and keep it alive for the duration of the test. Returns
  // a raw pointer suitable for placing into a `perFilterConfigs()` result.
  const Router::RouteSpecificFilterConfig* makePerRoute(const std::string& yaml) {
    FilterChainConfigProtoPerRoute proto_per_route;
    TestUtility::loadFromYaml(yaml, proto_per_route);
    auto config_or_error = factory_.createRouteSpecificFilterConfig(
        proto_per_route, context_.serverFactoryContext(), context_.messageValidationVisitor());
    EXPECT_TRUE(config_or_error.status().ok());
    per_route_configs_.push_back(config_or_error.value());
    return per_route_configs_.back().get();
  }

  // Wire up the callbacks so that the route returns the given per-route configs from
  // perFilterConfigs(). The configs must be ordered from least to most specific.
  void setRouteConfigs(const Router::RouteSpecificFilterConfigs& configs) {
    EXPECT_CALL(callbacks_, route())
        .WillOnce(Return(makeOptRefFromPtr<const Router::Route>(&route_)));
    EXPECT_CALL(route_, perFilterConfigs(FilterConfigName)).WillOnce(Return(configs));
  }

  FilterChainFilterFactory factory_;
  std::shared_ptr<MockFilterConfigFactory> mock_factory_;
  std::unique_ptr<Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory>>
      registry_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Http::FilterFactoryCb cb_;
  NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks_;
  NiceMock<Router::MockRoute> route_;
  std::vector<Router::RouteSpecificFilterConfigConstSharedPtr> per_route_configs_;
  std::vector<std::string> applied_filters_;
};

// No config and no route: the filter passes through.
TEST_F(FilterChainFactoryTest, EmptyConfig) {
  initialize("");

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_)).Times(0);
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 0);
  EXPECT_EQ(context_.store_.counter("test.filter_chain.pass_through").value(), 1);
}

// A route exists but provides no filter chain config and there is no default chain: pass through.
TEST_F(FilterChainFactoryTest, RouteWithoutFilterChainConfig) {
  initialize("");

  EXPECT_CALL(callbacks_, route())
      .WillOnce(Return(makeOptRefFromPtr<const Router::Route>(&route_)));
  EXPECT_CALL(route_, perFilterConfigs(FilterConfigName))
      .WillOnce(Return(Router::RouteSpecificFilterConfigs{}));
  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_)).Times(0);
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 0);
  EXPECT_EQ(context_.store_.counter("test.filter_chain.pass_through").value(), 1);
}

// Only the default filter chain is configured and there is no route.
TEST_F(FilterChainFactoryTest, DefaultFilterChain) {
  initialize(R"EOF(
    default_filter_chain:
      filters:
      - name: filter_a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_));
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 1);
  EXPECT_THAT(applied_filters_, ElementsAre("filter_a"));
  EXPECT_EQ(context_.store_.counter("test.filter_chain.pass_through").value(), 0);
}

// A single per-route filter chain is applied.
TEST_F(FilterChainFactoryTest, PerRouteFilterChain) {
  initialize("");

  setRouteConfigs({makePerRoute(R"EOF(
    filter_chain:
      filters:
      - name: filter_a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: filter_b
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF")});

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_)).Times(2);
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 2);
  EXPECT_THAT(applied_filters_, ElementsAre("filter_a", "filter_b"));
}

// The default chain and a per-route chain with disjoint filters are both applied. The default
// (least specific) chain runs first in decode order.
TEST_F(FilterChainFactoryTest, DefaultAndPerRouteMerged) {
  initialize(R"EOF(
    default_filter_chain:
      filters:
      - name: filter_a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");

  setRouteConfigs({makePerRoute(R"EOF(
    filter_chain:
      filters:
      - name: filter_b
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF")});

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_)).Times(2);
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 2);
  EXPECT_THAT(applied_filters_, ElementsAre("filter_a", "filter_b"));
}

// A more specific (per-route) chain overrides a same-named filter from a less specific (default)
// chain: the default's "shared" filter is skipped, and only the per-route one is applied.
TEST_F(FilterChainFactoryTest, PerRouteOverridesDefault) {
  initialize(R"EOF(
    default_filter_chain:
      filters:
      - name: shared
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: filter_a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");

  setRouteConfigs({makePerRoute(R"EOF(
    filter_chain:
      filters:
      - name: shared
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: filter_b
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF")});

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_)).Times(3);
  cb_(callbacks_);
  // The default's "shared" is overridden, so it is not applied. The default's unique "filter_a"
  // still runs first, then the per-route chain.
  EXPECT_EQ(mock_factory_->filter_added_, 3);
  EXPECT_THAT(applied_filters_, ElementsAre("filter_a", "shared", "filter_b"));
}

// Multiple per-route configs along the hierarchy (e.g. virtual host then route) are merged, with
// the most specific definition of a shared filter winning.
TEST_F(FilterChainFactoryTest, HierarchyOverride) {
  initialize("");

  // perFilterConfigs returns configs from least to most specific.
  setRouteConfigs({
      makePerRoute(R"EOF(
        filter_chain:
          filters:
          - name: common
            typed_config:
              "@type": type.googleapis.com/google.protobuf.Struct
          - name: vhost_only
            typed_config:
              "@type": type.googleapis.com/google.protobuf.Struct
      )EOF"),
      makePerRoute(R"EOF(
        filter_chain:
          filters:
          - name: common
            typed_config:
              "@type": type.googleapis.com/google.protobuf.Struct
          - name: route_only
            typed_config:
              "@type": type.googleapis.com/google.protobuf.Struct
      )EOF"),
  });

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_)).Times(3);
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 3);
  // "common" from the less specific config is overridden by the more specific one.
  EXPECT_THAT(applied_filters_, ElementsAre("vhost_only", "common", "route_only"));
}

// Non filter-chain per-route configs in the list are ignored.
TEST_F(FilterChainFactoryTest, IgnoresUnrelatedPerRouteConfig) {
  initialize("");

  // A bare RouteSpecificFilterConfig that is not a FilterChainPerRouteConfig.
  class OtherConfig : public Router::RouteSpecificFilterConfig {};
  OtherConfig other;

  setRouteConfigs({&other, makePerRoute(R"EOF(
    filter_chain:
      filters:
      - name: filter_a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF")});

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_));
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 1);
  EXPECT_THAT(applied_filters_, ElementsAre("filter_a"));
}

} // namespace
} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
