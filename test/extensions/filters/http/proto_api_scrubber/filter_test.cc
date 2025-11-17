#include <cstdint>
#include <memory>
#include <string>

#include "source/extensions/filters/http/proto_api_scrubber/filter.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
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
namespace ProtoApiScrubber {
namespace {

using ::apikeys::ApiKey;
using ::apikeys::CreateApiKeyRequest;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::checkSerializedData;
using ::Envoy::Http::MockStreamDecoderFilterCallbacks;
using ::Envoy::Http::MockStreamEncoderFilterCallbacks;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Protobuf::Struct;
using ::testing::Eq;

inline constexpr const char kApiKeysDescriptorRelativePath[] = "test/proto/apikeys.descriptor";

class ProtoApiScrubberFilterTest : public ::testing::Test {
protected:
  ProtoApiScrubberFilterTest() : api_(Api::createApiForTest()) { setup(); }

  void setup() {
    setupMocks();
    setupFilterConfig();
    setupFilter();
  }

  void setupMocks() {
    ON_CALL(mock_decoder_callbacks_, decoderBufferLimit())
        .WillByDefault(testing::Return(UINT32_MAX));

    ON_CALL(mock_encoder_callbacks_, encoderBufferLimit())
        .WillByDefault(testing::Return(UINT32_MAX));
  }

  void setupFilter() {
    filter_ = std::make_unique<ProtoApiScrubberFilter>(*filter_config_);
    filter_->setDecoderFilterCallbacks(mock_decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(mock_encoder_callbacks_);
  }

  void setupFilterConfig() {
    Protobuf::TextFormat::ParseFromString("", &proto_config_);
    *proto_config_.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
    auto config_or_status =
        ProtoApiScrubberFilterConfig::create(proto_config_, mock_factory_context_);
    ASSERT_TRUE(config_or_status.ok());

    filter_config_ = config_or_status.value();
  }

  void reloadFilterWithConfig(absl::string_view config_pb_text) {
    // 1. Reset the proto config object
    proto_config_.Clear();

    // 2. Parse the user-provided config string
    if (!config_pb_text.empty()) {
      Envoy::Protobuf::TextFormat::ParseFromString(std::string(config_pb_text), &proto_config_);
    }

    // 3. CRITICAL: Re-inject the descriptor set.
    // proto_config_.Clear() deleted the previous one.
    *proto_config_.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();

    // 4. Re-create the Filter Config
    auto config_or_status =
        ProtoApiScrubberFilterConfig::create(proto_config_, mock_factory_context_);
    ASSERT_TRUE(config_or_status.ok());
    filter_config_ = config_or_status.value();

    setupFilter();
  }

  void TearDown() override {
    // Test onDestroy doesn't crash.
    filter_->PassThroughDecoderFilter::onDestroy();
    filter_->PassThroughEncoderFilter::onDestroy();
  }

  apikeys::CreateApiKeyRequest makeCreateApiKeyRequest(absl::string_view pb = R"pb(
      parent: "project-id"
      key: {
        display_name: "Display Name"
        current_key: "current-key"
        create_time { seconds: 1684306560 nanos: 0 }
        update_time { seconds: 1684306560 nanos: 0 }
        location: "global"
        kms_key: "projects/my-project/locations/my-location"
        expire_time { seconds: 1715842560 nanos: 0 }
      }
    )pb") {
    apikeys::CreateApiKeyRequest request;
    Envoy::Protobuf::TextFormat::ParseFromString(pb, &request);
    return request;
  }

  void splitBuffer(Envoy::Buffer::InstancePtr& data, uint32_t start_size, uint32_t middle_size,
                   Envoy::Buffer::OwnedImpl& start, Envoy::Buffer::OwnedImpl& middle,
                   Envoy::Buffer::OwnedImpl& end) {
    start.move(*data, start_size);
    middle.move(*data, middle_size);
    end.move(*data);
    EXPECT_EQ(data->length(), 0);
  }

  Api::ApiPtr api_;
  ProtoApiScrubberConfig proto_config_;
  std::shared_ptr<const ProtoApiScrubberFilterConfig> filter_config_;
  testing::NiceMock<MockStreamDecoderFilterCallbacks> mock_decoder_callbacks_;
  testing::NiceMock<MockStreamEncoderFilterCallbacks> mock_encoder_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context_;
  std::unique_ptr<ProtoApiScrubberFilter> filter_;
};

// Following tests validate that the filter is not executed for requests with invalid headers.
using ProtoApiScrubberInvalidRequestHeaderTests = ProtoApiScrubberFilterTest;

TEST_F(ProtoApiScrubberInvalidRequestHeaderTests, RequestNotGrpc) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "not-grpc"}};

  // Pass through headers directly.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  // Pass through request data directly.
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue,
            filter_->decodeData(
                *Envoy::Grpc::Common::serializeToGrpcFrame(makeCreateApiKeyRequest()), true));
}

TEST_F(ProtoApiScrubberInvalidRequestHeaderTests, PathNotExist) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"}, {"content-type", "application/grpc"}};

  // Pass through headers directly.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  // Pass through request data directly.
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue,
            filter_->decodeData(
                *Envoy::Grpc::Common::serializeToGrpcFrame(makeCreateApiKeyRequest()), true));
}

// Following tests validate that the filter rejects the request for various failure scenarios.
using ProtoApiScrubberRequestRejectedTests = ProtoApiScrubberFilterTest;

TEST_F(ProtoApiScrubberRequestRejectedTests, BufferLimitedExceeded) {
  ON_CALL(mock_decoder_callbacks_, decoderBufferLimit()).WillByDefault(testing::Return(0));

  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  CreateApiKeyRequest request = makeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  EXPECT_CALL(mock_decoder_callbacks_,
              sendLocalReply(
                  Http::Code::BadRequest, "Rejected because internal buffer limits are exceeded.",
                  Eq(nullptr), Eq(Envoy::Grpc::Status::FailedPrecondition),
                  "proto_api_scrubber_FAILED_PRECONDITION{REQUEST_BUFFER_CONVERSION_FAIL}"));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(*request_data, true));
}

// Following tests validate that the request passes through the filter without any modification.
using ProtoApiScrubberPassThroughTest = ProtoApiScrubberFilterTest;

TEST_F(ProtoApiScrubberPassThroughTest, UnarySingleBuffer) {
  Envoy::Http::TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  CreateApiKeyRequest request = makeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  // No data modification.
  checkSerializedData<CreateApiKeyRequest>(*request_data, {request});
}

TEST_F(ProtoApiScrubberPassThroughTest, UnaryMultipeBuffers) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  CreateApiKeyRequest request = makeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  // Split into multiple buffers.
  const uint32_t req_data_size[] = {3, 4};
  Envoy::Buffer::OwnedImpl request_data_parts[3];
  splitBuffer(request_data, req_data_size[0], req_data_size[1], request_data_parts[0],
              request_data_parts[1], request_data_parts[2]);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_data_parts[0], false));
  EXPECT_EQ(request_data_parts[0].length(), 0);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_data_parts[1], false));
  EXPECT_EQ(request_data_parts[1].length(), 0);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue,
            filter_->decodeData(request_data_parts[2], true));

  // Inject data back and verify that no data modification.
  checkSerializedData<CreateApiKeyRequest>(request_data_parts[2], {request});
}

TEST_F(ProtoApiScrubberPassThroughTest, StreamingMultipleMessageSingleBuffer) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKeyInStream"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
  CreateApiKeyRequest request1 = makeCreateApiKeyRequest();
  CreateApiKeyRequest request2 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req2"
)pb");
  CreateApiKeyRequest request3 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req3"
)pb");

  Envoy::Buffer::InstancePtr request_data1 = Envoy::Grpc::Common::serializeToGrpcFrame(request1);
  Envoy::Buffer::InstancePtr request_data2 = Envoy::Grpc::Common::serializeToGrpcFrame(request2);
  Envoy::Buffer::InstancePtr request_data3 = Envoy::Grpc::Common::serializeToGrpcFrame(request3);

  // Split into multiple buffers.
  Envoy::Buffer::OwnedImpl request_data;
  request_data.move(*request_data1);
  request_data.move(*request_data2);
  request_data.move(*request_data3);
  EXPECT_EQ(request_data1->length(), 0);
  EXPECT_EQ(request_data2->length(), 0);
  EXPECT_EQ(request_data3->length(), 0);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(request_data, false));

  // Inject data back and expect no data modification.
  checkSerializedData<CreateApiKeyRequest>(request_data, {request1, request2, request3});

  // No op for the following messages.
  CreateApiKeyRequest request4 = makeCreateApiKeyRequest(
      R"pb(
      parent: "from-req4"
    )pb");
  Envoy::Buffer::InstancePtr request_data4 = Envoy::Grpc::Common::serializeToGrpcFrame(request4);
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data4, true));

  // No data modification.
  checkSerializedData<CreateApiKeyRequest>(*request_data4, {request4});
}

TEST_F(ProtoApiScrubberPassThroughTest, ScrubRequestFieldBasedOnConfig) {
  // 1. Define a configuration that scrubs 'key.display_name'
  //    for the method '/apikeys.ApiKeys/CreateApiKey'.
  const std::string config_with_restrictions = R"pb(
    filtering_mode: OVERRIDE
    restrictions {
      method_restrictions: {
        key: "/apikeys.ApiKeys/CreateApiKey"
        value: {
          request_field_restrictions: {
            key: "key.display_name"
            value: {
              matcher: {
                matcher_list: {
                  matchers: {
                    predicate: {
                      single_predicate: {
                        input: {
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] { }
                          }
                        }
                        custom_match: {
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                              expr_match: {
                                cel_expr_parsed: {
                                  expr: {
                                    id: 1
                                    const_expr: {
                                      bool_value: true
                                    }
                                  }
                                  source_info: {
                                    syntax_version: "cel1"
                                    location: "inline_expression"
                                    positions: {
                                      key: 1
                                      value: 0
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                    on_match: {
                      action: {
                        typed_config: {
                          [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  )pb";

  // 2. Reload the filter with this specific config
  reloadFilterWithConfig(config_with_restrictions);

  // 3. Prepare the request
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};

  // Create a request object that HAS the field we want to scrub
  // (makeCreateApiKeyRequest defaults "display_name" to "Display Name")
  CreateApiKeyRequest request = makeCreateApiKeyRequest();

  // Verify the field is present initially in our test object
  EXPECT_EQ(request.key().display_name(), "Display Name");

  // 4. Run the Filter
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  CreateApiKeyRequest expected_scrubbed_request = makeCreateApiKeyRequest();
  expected_scrubbed_request.mutable_key()->set_display_name("");

  checkSerializedData<CreateApiKeyRequest>(*request_data, {expected_scrubbed_request});
}

} // namespace

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
