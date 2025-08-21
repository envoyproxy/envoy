#include <memory>

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

TEST(ServiceMatchDataInputTest, ServiceMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  ServiceMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  MatchInput match_input(request, stream_info, MatchAction::RouteAction);

  EXPECT_EQ("", absl::get<std::string>(input->get(match_input).data_));

  request.host_ = "fake_host_as_service";

  EXPECT_EQ("fake_host_as_service", absl::get<std::string>(input->get(match_input).data_));
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

  EXPECT_EQ("", absl::get<std::string>(input->get(match_input).data_));

  request.host_ = "fake_host_as_service";

  EXPECT_EQ("fake_host_as_service", absl::get<std::string>(input->get(match_input).data_));
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

  EXPECT_EQ("", absl::get<std::string>(input->get(match_input).data_));

  request.path_ = "fake_path";

  EXPECT_EQ("fake_path", absl::get<std::string>(input->get(match_input).data_));
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

  EXPECT_EQ("", absl::get<std::string>(input->get(match_input).data_));

  request.method_ = "fake_method";

  EXPECT_EQ("fake_method", absl::get<std::string>(input->get(match_input).data_));
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

  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(input->get(match_input).data_));

  request.data_["key_0"] = "value_0";

  EXPECT_EQ("value_0", absl::get<std::string>(input->get(match_input).data_));
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

  auto custom_match_data =
      absl::get<std::shared_ptr<Matcher::CustomMatchData>>(input->get(match_input).data_);
  EXPECT_EQ(&match_input, &dynamic_cast<const RequestMatchData*>(custom_match_data.get())->data());
}

class FakeCustomMatchData : public Matcher::CustomMatchData {};

TEST(RequestMatchInputMatcherTest, RequestMatchInputMatcherTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  RequestMatchDataInputMatcherFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto matcher =
      factory.createInputMatcherFactoryCb(*proto_config, factory_context.serverFactoryContext())();

  EXPECT_EQ(*matcher->supportedDataInputTypes().begin(),
            "Envoy::Extensions::NetworkFilters::GenericProxy::RequestMatchData");

  {
    Matcher::MatchingDataType input;
    EXPECT_FALSE(matcher->match(input));
  }

  {
    Matcher::MatchingDataType input = std::string("fake_data");
    EXPECT_FALSE(matcher->match(input));
  }

  {
    Matcher::MatchingDataType input = std::make_shared<FakeCustomMatchData>();
    EXPECT_FALSE(matcher->match(input));
  }

  {
    FakeStreamCodecFactory::FakeRequest request;
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    MatchInput match_input(request, stream_info, MatchAction::RouteAction);

    Matcher::MatchingDataType input = std::make_shared<RequestMatchData>(match_input);
    EXPECT_TRUE(matcher->match(input));
  }
}

TEST(RequestMatchInputMatcherTest, SpecificRequestMatchInputMatcherTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  // Empty matcher.
  {
    RequestMatcherProto matcher_proto;
    RequestMatchInputMatcher matcher(matcher_proto, context.serverFactoryContext());

    FakeStreamCodecFactory::FakeRequest request;
    EXPECT_TRUE(matcher.match(request));
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
    EXPECT_FALSE(matcher.match(request));
  }

  // Path match failed.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "another_fake_path";
    EXPECT_FALSE(matcher.match(request));
  }

  // Method match failed.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "another_fake_method";
    EXPECT_FALSE(matcher.match(request));
  }

  // Property match failed.
  {

    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "fake_method";
    request.data_["key_0"] = "another_value_0";
    EXPECT_FALSE(matcher.match(request));
  }

  // Property is missing.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "fake_method";
    EXPECT_FALSE(matcher.match(request));
  }

  // All match.
  {
    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "fake_method";
    request.data_["key_0"] = "value_0";
    EXPECT_TRUE(matcher.match(request));
  }
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
