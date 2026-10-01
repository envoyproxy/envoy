#include <filesystem>

#include "envoy/registry/registry.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/matching/http/dynamic_modules/string_data_input.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace DynamicModules {
namespace {

using ::testing::NiceMock;

using DynamicModuleDataInputProto =
    envoy::extensions::matching::http::dynamic_modules::v3::DynamicModuleDataInput;

std::shared_ptr<Network::ConnectionInfoSetterImpl> connectionInfoProvider() {
  CONSTRUCT_ON_FIRST_USE(std::shared_ptr<Network::ConnectionInfoSetterImpl>,
                         std::make_shared<Network::ConnectionInfoSetterImpl>(
                             std::make_shared<Network::Address::Ipv4Instance>(80),
                             std::make_shared<Network::Address::Ipv4Instance>(80)));
}

StreamInfo::StreamInfoImpl createStreamInfo() {
  return StreamInfo::StreamInfoImpl(
      ::Envoy::Http::Protocol::Http2, Event::GlobalTimeSystem().timeSystem(),
      connectionInfoProvider(), StreamInfo::FilterState::LifeSpan::FilterChain);
}

class DynamicModuleDataInputTest : public testing::Test {
public:
  DynamicModuleDataInputTest() {
    const std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("matcher_string_input_echo", "c");
    const std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  // Builds a proto that loads the named module and configures it to echo the given request header.
  DynamicModuleDataInputProto protoConfig(absl::string_view module_name,
                                          absl::string_view header_name) {
    const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
input_name: test_input
input_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: {}
)EOF",
                                         module_name, header_name);
    DynamicModuleDataInputProto proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    return proto_config;
  }

  // Returns the number of times matcher_string_input_echo has destroyed its configuration. Holding
  // the module handle keeps the shared counter alive across the factory's own load of the module.
  std::function<int()> configDestroyCounter() {
    using GetConfigDestroyCountFuncType = int (*)();
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("matcher_string_input_echo", "c"),
        /*do_not_close=*/true);
    EXPECT_TRUE(module.ok());
    auto destroy_count = module.value()->getFunctionPointer<GetConfigDestroyCountFuncType>(
        "getStringDataInputConfigDestroyCount");
    EXPECT_TRUE(destroy_count.ok());
    return [module = std::shared_ptr(std::move(module.value())), count = destroy_count.value()]() {
      return count();
    };
  }

  DynamicModuleDataInputFactory factory_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

TEST_F(DynamicModuleDataInputTest, FactoryName) {
  EXPECT_EQ("envoy.matching.inputs.dynamic_module_string_data_input", factory_.name());
}

TEST_F(DynamicModuleDataInputTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleDataInputProto*>(proto.get()));
}

TEST_F(DynamicModuleDataInputTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<
      ::Envoy::Matcher::DataInputFactory<::Envoy::Http::HttpMatchingData>>::
      getFactory("envoy.matching.inputs.dynamic_module_string_data_input");
  EXPECT_NE(nullptr, factory);
}

TEST_F(DynamicModuleDataInputTest, ValidConfig) {
  auto factory_cb = factory_.createDataInputFactoryCb(
      protoConfig("matcher_string_input_echo", "x-route"), validation_visitor_);
  ASSERT_NE(nullptr, factory_cb);
  auto data_input = factory_cb();
  ASSERT_NE(nullptr, data_input);
  // The produced value is a string so map matchers can dispatch on it.
  EXPECT_EQ(::Envoy::Matcher::DefaultMatchingDataType, data_input->dataInputType());
}

// Load the module via the ``module.local.filename`` data source instead of by name.
TEST_F(DynamicModuleDataInputTest, ValidConfigWithLocalFile) {
  DynamicModuleDataInputProto proto_config;
  proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
      Extensions::DynamicModules::testSharedObjectPath("matcher_string_input_echo", "c"));
  proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
  proto_config.set_input_name("test_input");

  auto factory_cb = factory_.createDataInputFactoryCb(proto_config, validation_visitor_);
  ASSERT_NE(nullptr, factory_cb);
  EXPECT_NE(nullptr, factory_cb());
}

// Remote module sources are not supported for data inputs, which have no factory context.
TEST_F(DynamicModuleDataInputTest, RemoteSourceRejected) {
  DynamicModuleDataInputProto proto_config;
  auto* remote = proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_remote();
  remote->mutable_http_uri()->set_uri("https://example.com/module.so");
  remote->mutable_http_uri()->set_cluster("cluster_1");
  remote->mutable_http_uri()->mutable_timeout()->set_seconds(5);
  remote->set_sha256("abc123");
  proto_config.set_input_name("test_input");

  EXPECT_THROW_WITH_REGEX(factory_.createDataInputFactoryCb(proto_config, validation_visitor_),
                          EnvoyException, "Remote module sources require a factory context");
}

TEST_F(DynamicModuleDataInputTest, ConfigNewReturnsNull) {
  EXPECT_THROW_WITH_REGEX(
      factory_.createDataInputFactoryCb(
          protoConfig("matcher_string_input_config_new_fail", "x-route"), validation_visitor_),
      EnvoyException, "Failed to initialize dynamic module matcher data input config");
}

TEST_F(DynamicModuleDataInputTest, MissingConfigNew) {
  EXPECT_THROW_WITH_REGEX(
      factory_.createDataInputFactoryCb(
          protoConfig("matcher_string_input_missing_config_new", "x-route"), validation_visitor_),
      EnvoyException, "Failed to resolve symbol.*data_input_config_new");
}

TEST_F(DynamicModuleDataInputTest, MissingConfigDestroy) {
  EXPECT_THROW_WITH_REGEX(factory_.createDataInputFactoryCb(
                              protoConfig("matcher_string_input_missing_config_destroy", "x-route"),
                              validation_visitor_),
                          EnvoyException, "Failed to resolve symbol.*data_input_config_destroy");
}

TEST_F(DynamicModuleDataInputTest, MissingGet) {
  EXPECT_THROW_WITH_REGEX(
      factory_.createDataInputFactoryCb(protoConfig("matcher_string_input_missing_get", "x-route"),
                                        validation_visitor_),
      EnvoyException, "Failed to resolve symbol.*data_input_get");
}

TEST_F(DynamicModuleDataInputTest, MalformedInputConfig) {
  // The module loads and resolves symbols, but the input_config Any cannot be unpacked, which
  // happens before the module configuration is created.
  DynamicModuleDataInputProto proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("matcher_string_input_echo");
  proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
  proto_config.set_input_name("test_input");
  auto* any = proto_config.mutable_input_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("invalid_binary_data_that_cannot_be_unpacked_as_string_value");

  EXPECT_THROW_WITH_REGEX(factory_.createDataInputFactoryCb(proto_config, validation_visitor_),
                          EnvoyException, "Failed to parse data input config");
}

TEST_F(DynamicModuleDataInputTest, GetReturnsValueFromHeader) {
  auto factory_cb = factory_.createDataInputFactoryCb(
      protoConfig("matcher_string_input_echo", "x-route"), validation_visitor_);
  auto data_input = factory_cb();

  ::Envoy::Http::Matching::HttpMatchingDataImpl data(createStreamInfo());
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"x-route", "cluster_0"}};
  data.onRequestHeaders(request_headers);

  auto result = data_input->get(data);
  ASSERT_TRUE(result.stringData().has_value());
  EXPECT_EQ("cluster_0", result.stringData().value());
}

TEST_F(DynamicModuleDataInputTest, GetReturnsNoValueWhenHeaderAbsent) {
  auto factory_cb = factory_.createDataInputFactoryCb(
      protoConfig("matcher_string_input_echo", "x-route"), validation_visitor_);
  auto data_input = factory_cb();

  ::Envoy::Http::Matching::HttpMatchingDataImpl data(createStreamInfo());
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"x-other", "value"}};
  data.onRequestHeaders(request_headers);

  auto result = data_input->get(data);
  EXPECT_FALSE(result.stringData().has_value());
}

// Exercises the header lookup callback directly across every header map and edge case.
TEST_F(DynamicModuleDataInputTest, GetHeaderValueCallbackCoversAllPaths) {
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{
      {"x-single", "req"}, {"x-multi", "a"}, {"x-multi", "b"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{"x-resp", "resp"}};
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailers{{"x-trailer", "trailer"}};

  DataInputContext context;
  context.request_headers = &request_headers;
  context.response_headers = &response_headers;
  context.response_trailers = &response_trailers;

  envoy_dynamic_module_type_envoy_buffer result;
  size_t total = 0;

  // A request header value is returned and the total value count is reported.
  envoy_dynamic_module_type_module_buffer single_key = {"x-single", 8};
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_data_input_get_header_value(
      &context, envoy_dynamic_module_type_http_header_type_RequestHeader, single_key, &result, 0,
      &total));
  EXPECT_EQ("req", absl::string_view(result.ptr, result.length));
  EXPECT_EQ(1U, total);

  // A multi-value header is addressable by index and reports the full count.
  envoy_dynamic_module_type_module_buffer multi_key = {"x-multi", 7};
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_data_input_get_header_value(
      &context, envoy_dynamic_module_type_http_header_type_RequestHeader, multi_key, &result, 1,
      &total));
  EXPECT_EQ("b", absl::string_view(result.ptr, result.length));
  EXPECT_EQ(2U, total);

  // An index past the last value returns false.
  EXPECT_FALSE(envoy_dynamic_module_callback_matcher_data_input_get_header_value(
      &context, envoy_dynamic_module_type_http_header_type_RequestHeader, multi_key, &result, 2,
      nullptr));

  // The response header and response trailer maps are both reachable.
  envoy_dynamic_module_type_module_buffer resp_key = {"x-resp", 6};
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_data_input_get_header_value(
      &context, envoy_dynamic_module_type_http_header_type_ResponseHeader, resp_key, &result, 0,
      nullptr));
  EXPECT_EQ("resp", absl::string_view(result.ptr, result.length));

  envoy_dynamic_module_type_module_buffer trailer_key = {"x-trailer", 9};
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_data_input_get_header_value(
      &context, envoy_dynamic_module_type_http_header_type_ResponseTrailer, trailer_key, &result, 0,
      nullptr));
  EXPECT_EQ("trailer", absl::string_view(result.ptr, result.length));

  // An absent key reports zero values and returns false.
  envoy_dynamic_module_type_module_buffer absent_key = {"x-absent", 8};
  EXPECT_FALSE(envoy_dynamic_module_callback_matcher_data_input_get_header_value(
      &context, envoy_dynamic_module_type_http_header_type_RequestHeader, absent_key, &result, 0,
      &total));
  EXPECT_EQ(0U, total);

  // A header type with no backing map returns false and reports zero values.
  EXPECT_FALSE(envoy_dynamic_module_callback_matcher_data_input_get_header_value(
      &context, envoy_dynamic_module_type_http_header_type_RequestTrailer, single_key, &result, 0,
      &total));
  EXPECT_EQ(0U, total);
}

// The factory callback may run more than once, and every data input instance shares one in-module
// configuration. Releasing all of them must destroy the configuration exactly once.
TEST_F(DynamicModuleDataInputTest, SharedConfigDestroyedOnceForMultipleInputs) {
  const auto destroy_count = configDestroyCounter();
  const int before = destroy_count();

  {
    auto factory_cb = factory_.createDataInputFactoryCb(
        protoConfig("matcher_string_input_echo", "x-route"), validation_visitor_);
    auto input1 = factory_cb();
    auto input2 = factory_cb();
    ASSERT_NE(nullptr, input1);
    ASSERT_NE(nullptr, input2);
    // The configuration is still referenced, so it has not been destroyed yet.
    EXPECT_EQ(before, destroy_count());
  }

  EXPECT_EQ(before + 1, destroy_count());
}

// A factory callback that is never invoked still owns the in-module configuration and must destroy
// it once when released, so a rejected match tree does not leak the configuration.
TEST_F(DynamicModuleDataInputTest, ConfigDestroyedWhenFactoryCallbackNeverInvoked) {
  const auto destroy_count = configDestroyCounter();
  const int before = destroy_count();

  {
    auto factory_cb = factory_.createDataInputFactoryCb(
        protoConfig("matcher_string_input_echo", "x-route"), validation_visitor_);
  }

  EXPECT_EQ(before + 1, destroy_count());
}

} // namespace
} // namespace DynamicModules
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
