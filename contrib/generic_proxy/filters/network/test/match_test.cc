#include <memory>

#include "test/mocks/server/factory_context.h"

#include "contrib/generic_proxy/filters/network/source/match.h"
#include "contrib/generic_proxy/filters/network/test/fake_codec.h"
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

  EXPECT_EQ("", absl::get<std::string>(input->get(request).data_));

  request.host_ = "fake_host_as_service";

  EXPECT_EQ("fake_host_as_service", absl::get<std::string>(input->get(request).data_));
}

TEST(HostMatchDataInputTest, HostMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  HostMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;

  EXPECT_EQ("", absl::get<std::string>(input->get(request).data_));

  request.host_ = "fake_host_as_service";

  EXPECT_EQ("fake_host_as_service", absl::get<std::string>(input->get(request).data_));
}

TEST(PathMatchDataInputTest, PathMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  PathMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;

  EXPECT_EQ("", absl::get<std::string>(input->get(request).data_));

  request.path_ = "fake_path";

  EXPECT_EQ("fake_path", absl::get<std::string>(input->get(request).data_));
}

TEST(MethodMatchDataInputTest, MethodMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  MethodMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;

  EXPECT_EQ("", absl::get<std::string>(input->get(request).data_));

  request.method_ = "fake_method";

  EXPECT_EQ("fake_method", absl::get<std::string>(input->get(request).data_));
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

  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(input->get(request).data_));

  request.data_["key_0"] = "value_0";

  EXPECT_EQ("value_0", absl::get<std::string>(input->get(request).data_));
}

TEST(RequestMatchDataInputTest, RequestMatchDataInputTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  RequestMatchDataInputFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto input =
      factory.createDataInputFactoryCb(*proto_config, factory_context.messageValidationVisitor())();

  FakeStreamCodecFactory::FakeRequest request;

  EXPECT_EQ(
      &request,
      &dynamic_cast<const RequestMatchData*>(
           absl::get<std::shared_ptr<Matcher::CustomMatchData>>(input->get(request).data_).get())
           ->request());
}

TEST(RequestMatchInputMatcherTest, RequestMatchInputMatcherTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  RequestMatchDataInputMatcherFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto matcher =
      factory.createInputMatcherFactoryCb(*proto_config, factory_context.serverFactoryContext())();

  {
    Matcher::MatchingDataType input;
    EXPECT_FALSE(matcher->match(input));
  }

  {
    Matcher::MatchingDataType input = std::string("fake_data");
    EXPECT_FALSE(matcher->match(input));
  }

  {
    FakeStreamCodecFactory::FakeRequest request;
    Matcher::MatchingDataType input = std::make_shared<RequestMatchData>(request);
    EXPECT_TRUE(matcher->match(input));
  }
}

TEST(RequestMatchInputMatcherTest, SpecificRequestMatchInputMatcherTest) {
  // Empty matcher.
  {
    RequestMatcherProto matcher_proto;
    RequestMatchInputMatcher matcher(matcher_proto);

    FakeStreamCodecFactory::FakeRequest request;
    EXPECT_TRUE(matcher.match(request));
  }

  // Host match failed.
  {
    RequestMatcherProto matcher_proto;

    const std::string config_yaml = R"EOF(
    host:
      exact: fake_host
    )EOF";

    TestUtility::loadFromYaml(config_yaml, matcher_proto);

    RequestMatchInputMatcher matcher(matcher_proto);

    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "another_fake_host";
    EXPECT_FALSE(matcher.match(request));
  }

  // Path match failed.
  {
    RequestMatcherProto matcher_proto;

    const std::string config_yaml = R"EOF(
    host:
      exact: fake_host
    path:
      exact: fake_path
    )EOF";

    TestUtility::loadFromYaml(config_yaml, matcher_proto);

    RequestMatchInputMatcher matcher(matcher_proto);

    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "another_fake_path";
    EXPECT_FALSE(matcher.match(request));
  }

  // Method match failed.
  {
    RequestMatcherProto matcher_proto;

    const std::string config_yaml = R"EOF(
    host:
      exact: fake_host
    path:
      exact: fake_path
    method:
      exact: fake_method
    )EOF";

    TestUtility::loadFromYaml(config_yaml, matcher_proto);

    RequestMatchInputMatcher matcher(matcher_proto);

    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "another_fake_method";
    EXPECT_FALSE(matcher.match(request));
  }

  // Property match failed.
  {
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

    RequestMatchInputMatcher matcher(matcher_proto);

    FakeStreamCodecFactory::FakeRequest request;
    request.host_ = "fake_host";
    request.path_ = "fake_path";
    request.method_ = "fake_method";
    request.data_["key_0"] = "another_value_0";
    EXPECT_FALSE(matcher.match(request));
  }

  // All match.
  {
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

    RequestMatchInputMatcher matcher(matcher_proto);

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
