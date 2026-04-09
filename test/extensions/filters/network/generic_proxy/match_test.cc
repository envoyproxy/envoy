#include <memory>

#include "envoy/matcher/matcher.h"

#include "source/extensions/filters/network/generic_proxy/match.h"

#include "test/extensions/filters/network/generic_proxy/fake_codec.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

using ::Envoy::Matcher::DataInputGetResult;

TEST(ServiceMatchDataInputTest, ServiceMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  ServiceMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  MatchInput match_input(request, stream_info, MatchAction::RouteAction);

  auto result = input->get(match_input);
  EXPECT_EQ("", result.stringData().value());

  request.host_ = "fake_host_as_service";

  auto result2 = input->get(match_input);
  EXPECT_EQ("fake_host_as_service", result2.stringData().value());
}

TEST(HostMatchDataInputTest, HostMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  HostMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  MatchInput match_input(request, stream_info, MatchAction::RouteAction);

  auto result = input->get(match_input);
  EXPECT_EQ("", result.stringData().value());

  request.host_ = "fake_host_as_service";

  auto result2 = input->get(match_input);
  EXPECT_EQ("fake_host_as_service", result2.stringData().value());
}

TEST(PathMatchDataInputTest, PathMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  PathMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  MatchInput match_input(request, stream_info, MatchAction::RouteAction);

  auto result = input->get(match_input);
  EXPECT_EQ("", result.stringData().value());

  request.path_ = "fake_path";

  auto result2 = input->get(match_input);
  EXPECT_EQ("fake_path", result2.stringData().value());
}

TEST(MethodMatchDataInputTest, MethodMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  MethodMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  MatchInput match_input(request, stream_info, MatchAction::RouteAction);

  auto result = input->get(match_input);
  EXPECT_EQ("", result.stringData().value());

  request.method_ = "fake_method";

  auto result2 = input->get(match_input);
  EXPECT_EQ("fake_method", result2.stringData().value());
}

TEST(PropertyMatchDataInputTest, PropertyMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  PropertyMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();

  auto& typed_proto_config = static_cast<PropertyDataInputProto&>(*proto_config);

  typed_proto_config.set_property_name("key_0");

  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  MatchInput match_input(request, stream_info, MatchAction::RouteAction);

  auto result = input->get(match_input);
  EXPECT_EQ(absl::nullopt, result.stringData());

  request.data_["key_0"] = "value_0";

  auto result2 = input->get(match_input);
  EXPECT_EQ("value_0", result2.stringData().value());
}

TEST(RequestMatchDataInputTest, RequestMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  RequestMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  EXPECT_EQ(input->dataInputType(),
            "Envoy::Extensions::NetworkFilters::GenericProxy::RequestMatchData");

  FakeStreamCodecFactory::FakeRequest request;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  MatchInput match_input(request, stream_info, MatchAction::RouteAction);

  EXPECT_EQ(&request, &match_input.requestHeader());
  EXPECT_EQ(&stream_info, &match_input.streamInfo());
  EXPECT_EQ(MatchAction::RouteAction, match_input.expectAction());

  auto result = input->get(match_input);
  auto custom_match_data = result.customData<RequestMatchData>();
  ASSERT_TRUE(custom_match_data.has_value());
  EXPECT_EQ(&match_input, &custom_match_data->data());
}

class FakeCustomMatchData : public Matcher::CustomMatchData {};

TEST(RequestMatchInputMatcherTest, RequestMatchInputMatcherTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  RequestMatchDataInputMatcherFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto matcher =
      factory.createInputMatcherFactoryCb(*proto_config, factory_context.serverFactoryContext())();

  EXPECT_TRUE(matcher->supportsDataInputType(
      "Envoy::Extensions::NetworkFilters::GenericProxy::RequestMatchData"));

  { EXPECT_EQ(matcher->match(DataInputGetResult::NoData()), Matcher::MatchResult::NoMatch); }

  {
    EXPECT_EQ(matcher->match(DataInputGetResult::CreateString("fake_data")),
              Matcher::MatchResult::NoMatch);
  }

  {
    EXPECT_EQ(
        matcher->match(DataInputGetResult::CreateCustom(std::make_shared<FakeCustomMatchData>())),
        Matcher::MatchResult::NoMatch);
  }

  {
    FakeStreamCodecFactory::FakeRequest request;
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    MatchInput match_input(request, stream_info, MatchAction::RouteAction);

    EXPECT_EQ(matcher->match(DataInputGetResult::CreateCustom(
                  std::make_shared<RequestMatchData>(match_input))),
              Matcher::MatchResult::Matched);
  }
}

TEST(RequestMatchInputMatcherTest, SpecificRequestMatchInputMatcherTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  // Empty matcher.
  {
    RequestMatcherProto matcher_proto;
    RequestMatchInputMatcher matcher(matcher_proto, context.serverFactoryContext());

    FakeStreamCodecFactory::FakeRequest request;
    EXPECT_EQ(matcher.match(request), ::Envoy::Matcher::MatchResult::Matched);
  }

  RequestMatcherProto matcher_proto;

  const std::string config_yaml = R"EOF(
    host:
      exact: fake_host
    path:
      exact: fake_path
    method:
      exact: fake_method
    properties:
      - name: key_0
        string_match:
          exact: value_0
    )EOF";

  TestUtility::loadFromYaml(config_yaml, matcher_proto);

  RequestMatchInputMatcher matcher(matcher_proto, context.serverFactoryContext());

  // Host match failed.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "another_fake_host";
    EXPECT_EQ(matcher.match(request), ::Envoy::Matcher::MatchResult::NoMatch);
  }

  // Path match failed.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "another_fake_path";
    EXPECT_EQ(matcher.match(request), ::Envoy::Matcher::MatchResult::NoMatch);
  }

  // Method match failed.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "another_fake_method";
    EXPECT_EQ(matcher.match(request), ::Envoy::Matcher::MatchResult::NoMatch);
  }

  // Property match failed.
  {

    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "fake_method";
    request.data_["key_0"] = "another_value_0";
    EXPECT_EQ(matcher.match(request), ::Envoy::Matcher::MatchResult::NoMatch);
  }

  // Property is missing.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "fake_method";
    EXPECT_EQ(matcher.match(request), ::Envoy::Matcher::MatchResult::NoMatch);
  }

  // All match.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "fake_method";
    request.data_["key_0"] = "value_0";
    EXPECT_EQ(matcher.match(request), ::Envoy::Matcher::MatchResult::Matched);
  }
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
