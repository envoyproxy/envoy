#include "envoy/registry/registry.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/matching/http/dynamic_modules/data_input.h"

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

std::shared_ptr<Network::ConnectionInfoSetterImpl> connectionInfoProvider() {
  CONSTRUCT_ON_FIRST_USE(std::shared_ptr<Network::ConnectionInfoSetterImpl>,
                         std::make_shared<Network::ConnectionInfoSetterImpl>(
                             std::make_shared<Network::Address::Ipv4Instance>(80),
                             std::make_shared<Network::Address::Ipv4Instance>(80)));
}

StreamInfo::StreamInfoImpl createStreamInfo() {
  CONSTRUCT_ON_FIRST_USE(
      StreamInfo::StreamInfoImpl,
      StreamInfo::StreamInfoImpl(::Envoy::Http::Protocol::Http2,
                                 Event::GlobalTimeSystem().timeSystem(), connectionInfoProvider(),
                                 StreamInfo::FilterState::LifeSpan::FilterChain));
}

// =============================================================================
// DataInput Tests
// =============================================================================

TEST(HttpDynamicModuleDataInputTest, DataInputType) {
  HttpDynamicModuleDataInput input;
  EXPECT_EQ("dynamic_module_data_input", input.dataInputType());
}

TEST(HttpDynamicModuleDataInputTest, GetWithAllHeaders) {
  HttpDynamicModuleDataInput input;
  ::Envoy::Http::Matching::HttpMatchingDataImpl data(createStreamInfo());

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"x-test", "value"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{"content-type", "text/plain"}};
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailers{{"x-trailer", "done"}};

  data.onRequestHeaders(request_headers);
  data.onResponseHeaders(response_headers);
  data.onResponseTrailers(response_trailers);

  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_,
            ::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);

  auto* custom_data =
      absl::get_if<std::shared_ptr<::Envoy::Matcher::CustomMatchData>>(&result.data_);
  ASSERT_NE(nullptr, custom_data);

  auto* match_data = dynamic_cast<DynamicModuleMatchData*>(custom_data->get());
  ASSERT_NE(nullptr, match_data);

  EXPECT_NE(nullptr, match_data->request_headers_);
  EXPECT_NE(nullptr, match_data->response_headers_);
  EXPECT_NE(nullptr, match_data->response_trailers_);
}

TEST(HttpDynamicModuleDataInputTest, GetWithRequestHeadersOnly) {
  HttpDynamicModuleDataInput input;
  ::Envoy::Http::Matching::HttpMatchingDataImpl data(createStreamInfo());

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"x-test", "value"}};
  data.onRequestHeaders(request_headers);

  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_,
            ::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);

  auto* custom_data =
      absl::get_if<std::shared_ptr<::Envoy::Matcher::CustomMatchData>>(&result.data_);
  ASSERT_NE(nullptr, custom_data);

  auto* match_data = dynamic_cast<DynamicModuleMatchData*>(custom_data->get());
  ASSERT_NE(nullptr, match_data);

  EXPECT_NE(nullptr, match_data->request_headers_);
  EXPECT_EQ(nullptr, match_data->response_headers_);
  EXPECT_EQ(nullptr, match_data->response_trailers_);
}

TEST(HttpDynamicModuleDataInputTest, GetWithNoHeaders) {
  HttpDynamicModuleDataInput input;
  ::Envoy::Http::Matching::HttpMatchingDataImpl data(createStreamInfo());

  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_,
            ::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);

  auto* custom_data =
      absl::get_if<std::shared_ptr<::Envoy::Matcher::CustomMatchData>>(&result.data_);
  ASSERT_NE(nullptr, custom_data);

  auto* match_data = dynamic_cast<DynamicModuleMatchData*>(custom_data->get());
  ASSERT_NE(nullptr, match_data);

  EXPECT_EQ(nullptr, match_data->request_headers_);
  EXPECT_EQ(nullptr, match_data->response_headers_);
  EXPECT_EQ(nullptr, match_data->response_trailers_);
}

// =============================================================================
// DataInputFactory Tests
// =============================================================================

TEST(HttpDynamicModuleDataInputFactoryTest, FactoryName) {
  HttpDynamicModuleDataInputFactory factory;
  EXPECT_EQ("envoy.matching.inputs.dynamic_module_data_input", factory.name());
}

TEST(HttpDynamicModuleDataInputFactoryTest, CreateEmptyConfigProto) {
  HttpDynamicModuleDataInputFactory factory;
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(
      nullptr,
      dynamic_cast<
          envoy::extensions::matching::http::dynamic_modules::v3::HttpDynamicModuleMatchInput*>(
          proto.get()));
}

TEST(HttpDynamicModuleDataInputFactoryTest, CreateDataInputFactoryCb) {
  HttpDynamicModuleDataInputFactory factory;
  envoy::extensions::matching::http::dynamic_modules::v3::HttpDynamicModuleMatchInput config;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;

  auto factory_cb = factory.createDataInputFactoryCb(config, validation_visitor);
  auto data_input = factory_cb();
  EXPECT_NE(nullptr, data_input);
  EXPECT_EQ("dynamic_module_data_input", data_input->dataInputType());
}

TEST(HttpDynamicModuleDataInputFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<
      ::Envoy::Matcher::DataInputFactory<::Envoy::Http::HttpMatchingData>>::
      getFactory("envoy.matching.inputs.dynamic_module_data_input");
  EXPECT_NE(nullptr, factory);
}

} // namespace
} // namespace DynamicModules
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
