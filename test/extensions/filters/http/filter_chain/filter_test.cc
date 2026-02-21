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
using testing::NiceMock;
using testing::Return;

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
  }

  void initialize(const std::string& yaml_config, const std::string& per_route_yaml = "") {
    FilterChainConfigProto proto_config;
    if (!yaml_config.empty()) {
      TestUtility::loadFromYaml(yaml_config, proto_config);
    }
    auto callback_or_error = factory_.createFilterFactoryFromProto(proto_config, "test.", context_);
    EXPECT_TRUE(callback_or_error.status().ok());

    cb_ = callback_or_error.value();

    if (!per_route_yaml.empty()) {
      FilterChainConfigProtoPerRoute proto_per_route;
      TestUtility::loadFromYaml(per_route_yaml, proto_per_route);

      auto specific_config_or_error = factory_.createRouteSpecificFilterConfig(
          proto_per_route, context_.serverFactoryContext(), context_.messageValidationVisitor());
      EXPECT_TRUE(specific_config_or_error.status().ok());
      specific_config_ = specific_config_or_error.value();
    }
  }

  FilterChainFilterFactory factory_;
  std::shared_ptr<MockFilterConfigFactory> mock_factory_;
  std::unique_ptr<Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory>>
      registry_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Http::FilterFactoryCb cb_;
  NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks_;
  NiceMock<Router::MockRoute> route_;
  Router::RouteSpecificFilterConfigConstSharedPtr specific_config_;
};

TEST_F(FilterChainFactoryTest, EmptyConfig) {
  initialize("");

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_)).Times(0);
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 0);
  EXPECT_EQ(context_.store_.counter("test.filter_chain.no_route").value(), 1);
  EXPECT_EQ(context_.store_.counter("test.filter_chain.pass_through").value(), 1);
}

TEST_F(FilterChainFactoryTest, DefaultFilterChain) {
  const std::string yaml_config = R"EOF(
    filter_chain:
      filters:
      - name: mock_filter
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";
  initialize(yaml_config);

  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_));
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 1);
  EXPECT_EQ(context_.store_.counter("test.filter_chain.no_route").value(), 1);
  EXPECT_EQ(context_.store_.counter("test.filter_chain.use_default_filter_chain").value(), 1);
}

TEST_F(FilterChainFactoryTest, InlinedPerRouteConfig) {
  const std::string per_route_yaml = R"EOF(
    filter_chain:
      filters:
      - name: mock_filter
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: another_filter
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF";
  initialize("", per_route_yaml);

  EXPECT_CALL(callbacks_, route())
      .WillOnce(Return(makeOptRefFromPtr<const Router::Route>(&route_)));
  EXPECT_CALL(callbacks_, filterConfigName()).WillOnce(Return("envoy.filters.http.filter_chain"));
  EXPECT_CALL(route_, mostSpecificPerFilterConfig("envoy.filters.http.filter_chain"))
      .WillOnce(Return(specific_config_.get()));
  EXPECT_CALL(callbacks_, addStreamDecoderFilter(_)).Times(2);
  cb_(callbacks_);
  EXPECT_EQ(mock_factory_->filter_added_, 2);
  EXPECT_EQ(context_.store_.counter("test.filter_chain.use_route_filter_chain").value(), 1);
}

} // namespace
} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
