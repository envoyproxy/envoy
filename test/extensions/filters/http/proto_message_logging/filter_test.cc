#include <cstdint>
#include <memory>
#include <string>

#include "source/extensions/filters/http/proto_message_logging/extractor_impl.h"
#include "source/extensions/filters/http/proto_message_logging/filter.h"
#include "source/extensions/filters/http/proto_message_logging/filter_config.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {
namespace {

using ::apikeys::ApiKey;
using ::apikeys::CreateApiKeyRequest;
using ::envoy::extensions::filters::http::proto_message_logging::v3::ProtoMessageLoggingConfig;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::checkSerializedData;
using ::Envoy::Http::MockStreamDecoderFilterCallbacks;
using ::Envoy::Http::MockStreamEncoderFilterCallbacks;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::ProtobufWkt::Struct;

constexpr absl::string_view kFilterName = "envoy.filters.http.proto_message_logging";

constexpr absl::string_view kExpectedRequestMetadata = R"pb(
  fields {
    key: "requests"
    value {
      list_value {
        values {
          struct_value {
            fields {
              key: "@type"
              value {
                string_value: "type.googleapis.com/apikeys.CreateApiKeyRequest"
              }
            }
            fields {
              key: "parent"
              value { string_value: "project-id" }
            }
          }
        }
      }
    }
  }
)pb";

constexpr absl::string_view kExpectedResponseMetadata = R"pb(
  fields {
    key: "responses"
    value {
      list_value {
        values {
          struct_value {
            fields {
              key: "@type"
              value { string_value: "type.googleapis.com/apikeys.ApiKey" }
            }
            fields {
              key: "name"
              value { string_value: "apikey-name" }
            }
          }
        }
      }
    }
  }
)pb";

void checkProtoStruct(Envoy::ProtobufWkt::Struct got, absl::string_view expected_in_pbtext) {
  Envoy::ProtobufWkt::Struct expected;
  ASSERT_THAT(Envoy::Protobuf::TextFormat::ParseFromString(expected_in_pbtext, &expected), true);
  EXPECT_THAT(Envoy::TestUtility::protoEqual(got, expected, true), true)
      << "got:\n"
      << got.DebugString() << "expected:\n"
      << expected_in_pbtext;
}

class FilterTestBase : public ::testing::Test {
protected:
  FilterTestBase() : api_(Envoy::Api::createApiForTest()) {}

  void setUp(absl::string_view config = "") {
    ASSERT_THAT(Envoy::Protobuf::TextFormat::ParseFromString(
                    config.empty() ? protoConfig() : config, &proto_config_),
                true);

    *proto_config_.mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
            .value();

    ON_CALL(mock_decoder_callbacks_, decoderBufferLimit())
        .WillByDefault(testing::Return(UINT32_MAX));

    ON_CALL(mock_encoder_callbacks_, encoderBufferLimit())
        .WillByDefault(testing::Return(UINT32_MAX));

    filter_config_ = std::make_unique<FilterConfig>(
        proto_config_, std::make_unique<ExtractorFactoryImpl>(), *api_);

    filter_ = std::make_unique<Filter>(*filter_config_);
    filter_->setDecoderFilterCallbacks(mock_decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(mock_encoder_callbacks_);
  }

  void TearDown() override {
    // Test onDestroy doesn't crash.
    filter_->PassThroughDecoderFilter::onDestroy();
    filter_->PassThroughEncoderFilter::onDestroy();
  }

  virtual std::string protoConfig() {
    return R"pb(
      mode: FIRST_AND_LAST
      logging_by_method: {
        key: "apikeys.ApiKeys.CreateApiKey"
        value: {
          request_logging_by_field: { key: "parent" value: LOG }
          response_logging_by_field: { key: "name" value: LOG }
        }
      }
    )pb";
  }

  Envoy::Api::ApiPtr api_;
  std::string config_;
  ProtoMessageLoggingConfig proto_config_;
  std::unique_ptr<FilterConfig> filter_config_;
  testing::NiceMock<MockStreamDecoderFilterCallbacks> mock_decoder_callbacks_;
  testing::NiceMock<MockStreamEncoderFilterCallbacks> mock_encoder_callbacks_;
  std::unique_ptr<Filter> filter_;
};

apikeys::CreateApiKeyRequest makeCreateApiKeyRequest(absl::string_view pb = R"pb(
      parent: "project-id"
    )pb") {
  apikeys::CreateApiKeyRequest request;
  Envoy::Protobuf::TextFormat::ParseFromString(pb, &request);
  return request;
}

apikeys::ApiKey makeCreateApiKeyResponse(absl::string_view pb = R"pb(
  name: "apikey-name"
  display_name: "Display Name"
  current_key: "current-key"
  create_time { seconds: 1684306560 nanos: 0 }
  update_time { seconds: 1684306560 nanos: 0 }
  location: "global"
  kms_key: "projects/my-project/locations/my-location"
  expire_time { seconds: 1715842560 nanos: 0 }
)pb") {
  apikeys::ApiKey response;
  Envoy::Protobuf::TextFormat::ParseFromString(pb, &response);
  return response;
}

using FilterTestExtractOk = FilterTestBase;

TEST_F(FilterTestExtractOk, UnarySingleBuffer) {
  setUp();

  Envoy::Http::TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  CreateApiKeyRequest request = makeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(
          Invoke([](const std::string& ns, const Envoy::ProtobufWkt::Struct& new_dynamic_metadata) {
            EXPECT_EQ(ns, kFilterName);
            checkProtoStruct(new_dynamic_metadata, kExpectedRequestMetadata);
          }));

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  // No data modification.
  checkSerializedData<CreateApiKeyRequest>(*request_data, {request});

  Envoy::Http::TestResponseHeaderMapImpl resp_headers = TestResponseHeaderMapImpl{
      {":status", "200"},
      {"grpc-status", "1"},
      {"content-type", "application/grpc"},
  };
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(resp_headers, false));

  apikeys::ApiKey response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data = Envoy::Grpc::Common::serializeToGrpcFrame(response);

  EXPECT_CALL(mock_encoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(
          Invoke([](const std::string& ns, const Envoy::ProtobufWkt::Struct& new_dynamic_metadata) {
            EXPECT_EQ(ns, kFilterName);
            checkProtoStruct(new_dynamic_metadata, kExpectedResponseMetadata);
          }));

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->encodeData(*response_data, true));

  // No data modification.
  checkSerializedData<apikeys::ApiKey>(*response_data, {response});
}

TEST_F(FilterTestExtractOk, EmptyFields) {
  setUp();
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  CreateApiKeyRequest request = makeCreateApiKeyRequest("");
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns, const Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, kFilterName);
        checkProtoStruct(new_dynamic_metadata, R"pb(
          fields {
            key: "requests"
            value {
              list_value {
                values {
                  struct_value {
                    fields {
                      key: "@type"
                      value {
                        string_value: "type.googleapis.com/apikeys.CreateApiKeyRequest"
                      }
                    }
                  }
                }
              }
            }
          })pb");
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  // No data modification.
  checkSerializedData<CreateApiKeyRequest>(*request_data, {request});
}

TEST_F(FilterTestExtractOk, UnaryMultipeBuffers) {
  setUp();
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  const uint32_t start_data_size = 3;
  const uint32_t middle_data_size = 4;

  CreateApiKeyRequest request = makeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  // Split into multiple buffers.
  Envoy::Buffer::OwnedImpl start_request_data;
  Envoy::Buffer::OwnedImpl middle_request_data;
  Envoy::Buffer::OwnedImpl end_request_data;
  start_request_data.move(*request_data, start_data_size);
  middle_request_data.move(*request_data, middle_data_size);
  end_request_data.move(*request_data);
  EXPECT_EQ(request_data->length(), 0);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(start_request_data, false));
  EXPECT_EQ(start_request_data.length(), 0);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(middle_request_data, false));
  EXPECT_EQ(middle_request_data.length(), 0);

  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns, const Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, kFilterName);
        checkProtoStruct(new_dynamic_metadata, kExpectedRequestMetadata);
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(end_request_data, true));

  // Inject data back and no data modification.
  checkSerializedData<CreateApiKeyRequest>(end_request_data, {request});
}
} // namespace

} // namespace ProtoMessageLogging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
