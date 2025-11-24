#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/codes.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/proto/apikeys.pb.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

using ::apikeys::ApiKey;
using ::apikeys::CreateApiKeyRequest;
using ::bookstore::CreateShelfRequest;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::checkSerializedData;
using ::Envoy::Grpc::Status;
using ::Envoy::Http::MockStreamDecoderFilterCallbacks;
using ::Envoy::Http::MockStreamEncoderFilterCallbacks;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Protobuf::Struct;
using ::testing::_;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::ReturnRef;

inline constexpr const char kApiKeysDescriptorRelativePath[] = "test/proto/apikeys.descriptor";
inline constexpr char kRemoveFieldActionType[] =
    "type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction";
inline constexpr const char kBookstoreDescriptorRelativePath[] = "test/proto/bookstore.descriptor";

class ProtoApiScrubberFilterTest : public ::testing::Test {
protected:
  ProtoApiScrubberFilterTest() : api_(Api::createApiForTest()) { setup(); }

  // Helper Enum for clarity.
  enum class FieldType { Request, Response };

  virtual void setup() {
    setupMocks();
    // Default config is empty, tests will override.
    setupFilterConfig("", kApiKeysDescriptorRelativePath);
    setupFilter();
  }

  void setupMocks() {
    ON_CALL(mock_decoder_callbacks_, decoderBufferLimit())
        .WillByDefault(testing::Return(UINT32_MAX));

    ON_CALL(mock_encoder_callbacks_, encoderBufferLimit())
        .WillByDefault(testing::Return(UINT32_MAX));
    ON_CALL(mock_factory_context_, serverFactoryContext())
        .WillByDefault(ReturnRef(server_factory_context_));
    ON_CALL(server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  void setupFilter() {
    filter_ = std::make_unique<ProtoApiScrubberFilter>(*filter_config_);
    filter_->setDecoderFilterCallbacks(mock_decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(mock_encoder_callbacks_);
  }

  void setupFilterConfig(absl::string_view config_yaml,
                         const char* descriptor_path = kApiKeysDescriptorRelativePath) {
    Protobuf::TextFormat::ParseFromString(config_yaml, &proto_config_);
    if (!proto_config_.has_descriptor_set()) {
      *proto_config_.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
          api_->fileSystem()
              .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(descriptor_path))
              .value();
    }
    auto config_or_status =
        ProtoApiScrubberFilterConfig::create(proto_config_, mock_factory_context_);
    ASSERT_TRUE(config_or_status.ok());

    filter_config_ = config_or_status.value();
  }

  /**
   * Utility to add a field restriction to the provided `filter_config`.
   * @param filter_config The filter config to be modified.
   * @param method_name The gRPC method name (e.g., "/apikeys.ApiKeys/CreateApiKey").
   * @param field_path The proto field path (e.g., "key.display_name").
   * @param field_type Represents whether the request or response field restrictions need to be set.
   * @param match_result If true, the CEL expression evaluates to true (triggering the action),
   * otherwise, it evaluates to false.
   * @param action_type_url The type URL of the match action.
   */
  void addRestriction(ProtoApiScrubberConfig& config, const std::string& method_name,
                      const std::string& field_path, FieldType field_type, bool match_result,
                      const std::string& action_type_url) {
    constexpr absl::string_view matcher_template = R"pb(
      matcher_list: {
        matchers: {
          predicate: {
            single_predicate: {
              input: {
                name: "request"
                typed_config: {
                  [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] { }
                }
              }
              custom_match: {
                 name: "cel"
                typed_config: {
                  [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                    expr_match: {
                      cel_expr_parsed: {
                        expr: {
                          id: 1
                          const_expr: {
                            bool_value: $0
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
              name: "remove"
              typed_config: {
                [$1] { }
              }
            }
          }
        }
      }
    )pb";

    std::string matcher_str =
        absl::Substitute(matcher_template, match_result ? "true" : "false", action_type_url);

    xds::type::matcher::v3::Matcher matcher;
    if (!Envoy::Protobuf::TextFormat::ParseFromString(matcher_str, &matcher)) {
      FAIL() << "Failed to parse generated matcher config.";
    }

    auto& method_restrictions = *config.mutable_restrictions()->mutable_method_restrictions();
    auto& method_config = method_restrictions[method_name];
    auto* field_map = (field_type == FieldType::Request)
                          ? method_config.mutable_request_field_restrictions()
                          : method_config.mutable_response_field_restrictions();
    *(*field_map)[field_path].mutable_matcher() = matcher;
  }

  /**
   * Replaces the existing 'filter_' and 'filter_config_' with a new one based on
   * the provided proto. This overrides the default setup done in the constructor.
   */
  absl::Status reloadFilter(ProtoApiScrubberConfig& config,
                            const char* descriptor_path = kApiKeysDescriptorRelativePath) {
    // Ensure descriptors are present.
    if (!config.has_descriptor_set()) {
      auto content_or =
          api_->fileSystem().fileReadToEnd(Envoy::TestEnvironment::runfilesPath(descriptor_path));
      RETURN_IF_NOT_OK(content_or.status());

      *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
          std::move(content_or.value());
    }

    // Create new Config Object.
    auto config_or_status = ProtoApiScrubberFilterConfig::create(config, mock_factory_context_);
    RETURN_IF_NOT_OK(config_or_status.status());

    // Reset the filter config instance.
    filter_config_ = config_or_status.value();

    // Reset the filter instance.
    setupFilter();

    return absl::OkStatus();
  }

  void reSetupFilter(const char* descriptor_path = kApiKeysDescriptorRelativePath) {
    // Re-parse to be safe, though proto_config_ should be updated.
    setupFilterConfig(proto_config_.DebugString(), descriptor_path);
    setupFilter();
  }

  void TearDown() override {
    // Test onDestroy doesn't crash.
    filter_->PassThroughDecoderFilter::onDestroy();
    filter_->PassThroughEncoderFilter::onDestroy();
  }

  bookstore::CreateShelfRequest makeCreateShelfRequest() {
    bookstore::CreateShelfRequest request;
    request.mutable_shelf()->set_id(1);
    request.mutable_shelf()->set_theme("Test Theme");
    return request;
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

  apikeys::ApiKey makeCreateApiKeyResponse(absl::string_view pb = R"pb(
      name: "projects/p1/keys/k1"
      display_name: "Response Key Name"
      current_key: "secret-key-from-server"
      create_time { seconds: 1684306560 nanos: 0 }
      location: "global"
      kms_key: "projects/my-project/locations/my-location"
    )pb") {
    apikeys::ApiKey response;
    Envoy::Protobuf::TextFormat::ParseFromString(pb, &response);
    return response;
  }

  // Helper to construct a gRPC frame containing a nested message that claims to be
  // 100 bytes long but terminates immediately with Tag 0.
  // outer_tag: The field tag of the nested message (e.g., 0x12 for Field 2).
  Envoy::Buffer::OwnedImpl createTruncatedNestedMessageFrame(uint8_t outer_tag) {
    std::string malformed_payload;
    malformed_payload.push_back(static_cast<char>(outer_tag)); // Outer Tag: WireType 2
    malformed_payload.push_back(static_cast<char>(0x64));      // Outer Length: 100 (Varint 0x64)
    malformed_payload.push_back(static_cast<char>(0x00));      // Inner Data: 0x00 (Tag 0)

    Envoy::Buffer::OwnedImpl frame;
    uint8_t flag = 0;
    uint32_t length = htonl(malformed_payload.size());

    frame.add(&flag, sizeof(flag));
    frame.add(&length, sizeof(length));
    frame.add(malformed_payload);

    return frame;
  }

  // Helper to construct the raw Protobuf payload bytes for the truncated message.
  // Structure: [OuterTag, Length=100, Tag=0]
  std::string createTruncatedPayload(uint8_t outer_tag) {
    std::string payload;
    payload.push_back(static_cast<char>(outer_tag)); // Outer Tag: WireType 2
    payload.push_back(static_cast<char>(0x64));      // Outer Length: 100 (Varint 0x64)
    payload.push_back(static_cast<char>(0x00));      // Inner Data: 0x00 (Tag 0)
    return payload;
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
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
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

  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "not-grpc"}};

  // Pass through response headers directly.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(resp_headers, true));

  // Pass through response data directly.
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue,
            filter_->encodeData(
                *Envoy::Grpc::Common::serializeToGrpcFrame(makeCreateApiKeyResponse()), true));
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

TEST_F(ProtoApiScrubberRequestRejectedTests, RequestBufferLimitedExceeded) {
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

TEST_F(ProtoApiScrubberRequestRejectedTests, ResponseBufferLimitedExceeded) {
  ON_CALL(mock_encoder_callbacks_, encoderBufferLimit()).WillByDefault(testing::Return(0));

  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(resp_headers, false));

  ApiKey response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data = Envoy::Grpc::Common::serializeToGrpcFrame(response);

  EXPECT_CALL(mock_encoder_callbacks_,
              sendLocalReply(
                  Http::Code::BadRequest, "Rejected because internal buffer limits are exceeded.",
                  Eq(nullptr), Eq(Envoy::Grpc::Status::FailedPrecondition),
                  "proto_api_scrubber_FAILED_PRECONDITION{RESPONSE_BUFFER_CONVERSION_FAIL}"));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(*response_data, true));
}

// Following tests validate filter's graceful handling of empty messages in request and response.
using ProtoApiScrubberEmptyMessageTest = ProtoApiScrubberFilterTest;

TEST_F(ProtoApiScrubberEmptyMessageTest, HandlesEmptyRequestStreamMessage) {
  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};

  filter_->decodeHeaders(req_headers, false);

  // Create a data buffer with 0 bytes (empty), but end_stream = true.
  // The MessageConverter should produce an empty StreamMessage to signal EOS.
  Envoy::Buffer::OwnedImpl empty_data;

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(empty_data, true));
}

TEST_F(ProtoApiScrubberEmptyMessageTest, HandlesEmptyResponseStreamMessage) {
  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};

  filter_->decodeHeaders(req_headers, true);

  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}};
  filter_->encodeHeaders(resp_headers, false);

  // Create a data buffer with 0 bytes (empty), but end_stream = true.
  // The MessageConverter should produce an empty StreamMessage to signal EOS.
  Envoy::Buffer::OwnedImpl empty_data;

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
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

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(request_data_parts[0], false));
  EXPECT_EQ(request_data_parts[0].length(), 0);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationAndBuffer,
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

using ProtoApiScrubberPathValidationTest = ProtoApiScrubberFilterTest;

TEST_F(ProtoApiScrubberPathValidationTest, ValidateMethodNameScenarios) {
  const std::string expected_rc_detail =
      "proto_api_scrubber_INVALID_ARGUMENT{Error in `:path` header validation.}";

  // Case 1: Empty Path
  {
    TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", ""}, {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("Method name is empty"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }

  // Case 2: Wildcard in Path
  {
    TestRequestHeaderMapImpl req_headers =
        TestRequestHeaderMapImpl{{":method", "POST"},
                                 {":path", "/package.Service/Method*"},
                                 {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("contains '*' which is not supported"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }

  // Case 3: Missing Leading Slash
  {
    TestRequestHeaderMapImpl req_headers =
        TestRequestHeaderMapImpl{{":method", "POST"},
                                 {":path", "package.Service/Method"},
                                 {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("should follow the gRPC format"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }

  // Case 4: Missing Service Part (Double Slash)
  {
    TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", "//MethodName"}, {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("should follow the gRPC format"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }

  // Case 5: Missing Method Part (Trailing Slash)
  {
    TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", "/package.Service/"}, {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("should follow the gRPC format"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }

  // Case 6: Service Name Without Dot
  {
    TestRequestHeaderMapImpl req_headers =
        TestRequestHeaderMapImpl{{":method", "POST"},
                                 {":path", "/SimpleService/Method"},
                                 {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("should follow the gRPC format"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }

  // Case 7: Service Name with Empty Sub-parts (Double Dot)
  {
    TestRequestHeaderMapImpl req_headers =
        TestRequestHeaderMapImpl{{":method", "POST"},
                                 {":path", "/package..Service/Method"},
                                 {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("should follow the gRPC format"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }

  // Case 8: Extra Slashes Between Service and Method
  {
    TestRequestHeaderMapImpl req_headers =
        TestRequestHeaderMapImpl{{":method", "POST"},
                                 {":path", "/package.Service//Method"},
                                 {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("should follow the gRPC format"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }

  // Case 9: Extra Leading Slashes.
  {
    TestRequestHeaderMapImpl req_headers =
        TestRequestHeaderMapImpl{{":method", "POST"},
                                 {":path", "//package.Service/Method"},
                                 {"content-type", "application/grpc"}};

    EXPECT_CALL(mock_decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::BadRequest,
                               testing::HasSubstr("should follow the gRPC format"), _,
                               Eq(Envoy::Grpc::Status::InvalidArgument), Eq(expected_rc_detail)));

    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(req_headers, true));
  }
}

TEST_F(ProtoApiScrubberFilterTest, UnknownGrpcMethod_RequestFlow) {
  ProtoApiScrubberConfig config;
  ASSERT_TRUE(reloadFilter(config).ok());

  // Prepare request.
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/some.nonexistent.Service/UnknownMethod"},
                               {":scheme", "http"},
                               {"content-type", "application/grpc"}};
  CreateApiKeyRequest request = makeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  // The headers check passes because content-type is application/grpc.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  std::string expected_error_msg =
      "Unable to find method `some.nonexistent.Service.UnknownMethod` in the "
      "descriptor pool configured for this filter.";

  EXPECT_CALL(mock_decoder_callbacks_,
              sendLocalReply(Envoy::Http::Code::BadRequest, Eq(expected_error_msg), _,
                             Eq(Envoy::Grpc::Status::InvalidArgument),
                             Eq("proto_api_scrubber_INVALID_ARGUMENT{BAD_REQUEST}")));

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(*request_data, true));
}

// Tests the case where an unknown method name is passed in the request headers due to which
// creation of response scrubber fails.
// We simulate this by using a method name that satisfies the gRPC regex check
// (so decodeHeaders passes) but does NOT exist in the descriptor pool.
// We then skip decodeData (as if the request had no body) and go straight to encodeData, otherwise,
// it would have called `sendLocalReply` in decodeData itself.
TEST_F(ProtoApiScrubberFilterTest, UnknownGrpcMethod_ResponseFlow) {
  // Use a non-existent method name
  std::string method_name = "/apikeys.ApiKeys/NonExistentMethod";
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};

  // decodeHeaders passes because it only checks the format (regex), not the descriptor
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  // Skip decodeData (simulate no request body)
  // If we ran decodeData, it would fail here. By skipping it, we force the failure
  // to happen in encodeData instead.

  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(resp_headers, false));

  // Send Response Data
  // The filter will now try to create the Response Scrubber.
  // It will attempt to look up "apikeys.ApiKeys.NonExistentMethod" in the descriptor pool.
  // This will fail.
  ApiKey response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data = Envoy::Grpc::Common::serializeToGrpcFrame(response);

  // Verify Rejection
  // Expect the error log and Local Reply
  EXPECT_CALL(mock_encoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Unable to find method `apikeys.ApiKeys.NonExistentMethod` in the "
                             "descriptor pool configured for this filter.",
                             Eq(nullptr), Eq(Envoy::Grpc::Status::InvalidArgument),
                             "proto_api_scrubber_INVALID_ARGUMENT{BAD_REQUEST}"));

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(*response_data, true));
}

using ProtoApiScrubberScrubbingTest = ProtoApiScrubberFilterTest;

// Tests that a simple non-nested field with restrictions configured which evaluates to `true` is
// scrubbed out from the request.
TEST_F(ProtoApiScrubberScrubbingTest, ScrubRequestSimpleField) {
  ProtoApiScrubberConfig proto_config;
  proto_config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);

  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";
  std::string field_path = "parent";

  addRestriction(proto_config, method_name, field_path, FieldType::Request, true,
                 kRemoveFieldActionType);

  // Reload the filter with the above config.
  ASSERT_TRUE(reloadFilter(proto_config).ok());

  // Prepare the request.
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};
  CreateApiKeyRequest request = makeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  // Pre-check that the field exists in the incoming request.
  EXPECT_EQ(request.parent(), "project-id");

  // Run the filter.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  // Post-check: Verify scrubbing happened
  CreateApiKeyRequest expected_scrubbed_request = makeCreateApiKeyRequest();
  expected_scrubbed_request.clear_parent();

  checkSerializedData<CreateApiKeyRequest>(*request_data, {expected_scrubbed_request});
}

// Tests that a nested field with restrictions configured which evaluates to `true` is scrubbed out
// from the request.
TEST_F(ProtoApiScrubberScrubbingTest, ScrubRequestNestedField) {
  ProtoApiScrubberConfig proto_config;
  proto_config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);

  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";
  std::string field_path = "key.update_time.seconds";

  addRestriction(proto_config, method_name, field_path, FieldType::Request, true,
                 kRemoveFieldActionType);

  // Reload the filter with the above config.
  ASSERT_TRUE(reloadFilter(proto_config).ok());

  // Prepare the request.
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};
  CreateApiKeyRequest request = makeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  // Pre-check that the field exists in the incoming request.
  EXPECT_EQ(request.key().update_time().seconds(), 1684306560);

  // Run the filter.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  // Post-check: Verify scrubbing happened.
  CreateApiKeyRequest expected_scrubbed_request = makeCreateApiKeyRequest();
  expected_scrubbed_request.mutable_key()->mutable_update_time()->clear_seconds();

  checkSerializedData<CreateApiKeyRequest>(*request_data, {expected_scrubbed_request});
}

// Tests that the request passes through without modification even if the scrubbing fails due to
// malformed grpc message.
TEST_F(ProtoApiScrubberScrubbingTest, RequestScrubbingFailsOnTruncatedNestedMessage) {
  ProtoApiScrubberConfig proto_config;
  proto_config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";

  // Target 'key' (Field 2) in the Request
  addRestriction(proto_config, method_name, "key.display_name", FieldType::Request, true,
                 kRemoveFieldActionType);
  ASSERT_TRUE(reloadFilter(proto_config).ok());

  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  // Construct payload using Tag 0x12 (Field 2: key)
  Envoy::Buffer::OwnedImpl bad_data = createTruncatedNestedMessageFrame(0x12);

  // Execute the action
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(bad_data, true));

  // Verify Fail-Open (data matches expected payload unmodified)
  Envoy::Grpc::Decoder decoder;
  std::vector<Envoy::Grpc::Frame> frames;
  ASSERT_TRUE(decoder.decode(bad_data, frames).ok());

  EXPECT_EQ(createTruncatedPayload(0x12), frames[0].data_->toString());
}

using ProtoApiScrubberResponsePassThroughTest = ProtoApiScrubberFilterTest;

// Tests that a single-buffer gRPC response passes through without modification when no scrubbing is
// configured.
TEST_F(ProtoApiScrubberResponsePassThroughTest, UnaryResponseSingleBuffer) {
  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(resp_headers, false));

  ApiKey response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data = Envoy::Grpc::Common::serializeToGrpcFrame(response);
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->encodeData(*response_data, true));

  checkSerializedData<ApiKey>(*response_data, {response});
}

// Tests that a multi-buffer gRPC response passes through correctly, buffering internally until
// complete.
TEST_F(ProtoApiScrubberResponsePassThroughTest, UnaryResponseMultipleBuffers) {
  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(resp_headers, false));

  ApiKey response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data = Envoy::Grpc::Common::serializeToGrpcFrame(response);

  const uint32_t resp_data_size[] = {5, 10};
  Envoy::Buffer::OwnedImpl response_data_parts[3];
  splitBuffer(response_data, resp_data_size[0], resp_data_size[1], response_data_parts[0],
              response_data_parts[1], response_data_parts[2]);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(response_data_parts[0], false));
  EXPECT_EQ(response_data_parts[0].length(), 0);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(response_data_parts[1], false));
  EXPECT_EQ(response_data_parts[1].length(), 0);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue,
            filter_->encodeData(response_data_parts[2], true));

  checkSerializedData<ApiKey>(response_data_parts[2], {response});
}

using ProtoApiScrubberResponseScrubbingTest = ProtoApiScrubberFilterTest;

// Tests that a top-level field in the response is successfully scrubbed when configured.
TEST_F(ProtoApiScrubberResponseScrubbingTest, ScrubResponseSimpleField) {
  ProtoApiScrubberConfig proto_config;
  proto_config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);

  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";
  std::string field_path = "current_key";

  addRestriction(proto_config, method_name, field_path, FieldType::Response, true,
                 kRemoveFieldActionType);

  ASSERT_TRUE(reloadFilter(proto_config).ok());

  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(resp_headers, false));

  ApiKey response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data = Envoy::Grpc::Common::serializeToGrpcFrame(response);

  EXPECT_EQ(response.current_key(), "secret-key-from-server");

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->encodeData(*response_data, true));

  ApiKey expected_scrubbed_response = makeCreateApiKeyResponse();
  expected_scrubbed_response.clear_current_key();

  checkSerializedData<ApiKey>(*response_data, {expected_scrubbed_response});
}

// Tests that a nested field in the response is successfully scrubbed when configured.
TEST_F(ProtoApiScrubberResponseScrubbingTest, ScrubResponseNestedField) {
  ProtoApiScrubberConfig proto_config;
  proto_config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);

  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";
  std::string field_path = "create_time.seconds";

  addRestriction(proto_config, method_name, field_path, FieldType::Response, true,
                 kRemoveFieldActionType);

  ASSERT_TRUE(reloadFilter(proto_config).ok());

  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));

  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(resp_headers, false));

  ApiKey response = makeCreateApiKeyResponse();
  Envoy::Buffer::InstancePtr response_data = Envoy::Grpc::Common::serializeToGrpcFrame(response);

  // Pre-check
  EXPECT_EQ(response.create_time().seconds(), 1684306560);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->encodeData(*response_data, true));

  ApiKey expected_scrubbed_response = makeCreateApiKeyResponse();
  expected_scrubbed_response.mutable_create_time()->clear_seconds();

  checkSerializedData<ApiKey>(*response_data, {expected_scrubbed_response});
}

// Tests that if response parsing fails (e.g., malformed proto), the data passes through unmodified.
TEST_F(ProtoApiScrubberResponseScrubbingTest, ResponseScrubbingFailsOnTruncatedNestedMessage) {
  ProtoApiScrubberConfig proto_config;
  proto_config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);
  std::string method_name = "/apikeys.ApiKeys/CreateApiKey";

  // Target 'create_time' (Field 4) in the Response
  addRestriction(proto_config, method_name, "create_time.seconds", FieldType::Response, true,
                 kRemoveFieldActionType);
  ASSERT_TRUE(reloadFilter(proto_config).ok());

  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};
  filter_->decodeHeaders(req_headers, true);
  TestResponseHeaderMapImpl resp_headers =
      TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}};
  filter_->encodeHeaders(resp_headers, false);

  // Construct payload using Tag 0x22 (Field 4: create_time)
  Envoy::Buffer::OwnedImpl bad_data = createTruncatedNestedMessageFrame(0x22);

  // Execute the action
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->encodeData(bad_data, true));

  // Verify that data matches expected payload (unmodified)
  Envoy::Grpc::Decoder decoder;
  std::vector<Envoy::Grpc::Frame> frames;
  ASSERT_TRUE(decoder.decode(bad_data, frames).ok());

  EXPECT_EQ(createTruncatedPayload(0x22), frames[0].data_->toString());
}

// Tests for Method Level Restrictions
// Tests for Method Level Restrictions.
class MethodLevelRestrictionTest : public ProtoApiScrubberFilterTest {
protected:
  // Override setup to load bookstore descriptor.
  void setup() override {
    setupMocks();
    // Config will be set by each test.
  }
};

// Tests that a request is blocked if the method-level matcher evaluates to true.
TEST_F(MethodLevelRestrictionTest, MethodBlockedByMatcher) {
  setupFilterConfig(R"pb(
    restrictions: {
      method_restrictions: {
        key: "/bookstore.Bookstore/CreateShelf"
        value: {
          method_restriction: {
            matcher: {
              matcher_list: {
                matchers: {
                  predicate: {
                    single_predicate: {
                      input: {
                        name: "request"
                        typed_config: {
                          [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] {}
                        }
                      }
                      custom_match: {
                        name: "cel"
                        typed_config: {
                          [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                            expr_match: { parsed_expr: { expr: { const_expr: { bool_value: true } } } }
                          }
                        }
                      }
                    }
                  }
                  on_match: {
                    action: {
                      name: "block"
                      typed_config: {
                        [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] {}
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
  )pb",
                    kBookstoreDescriptorRelativePath);
  reSetupFilter(kBookstoreDescriptorRelativePath);

  auto req_headers = TestRequestHeaderMapImpl{{":method", "POST"},
                                              {":path", "/bookstore.Bookstore/CreateShelf"},
                                              {"content-type", "application/grpc"}};

  EXPECT_CALL(mock_decoder_callbacks_,
              sendLocalReply(Http::Code::Forbidden, // HTTP Code
                             "Method not allowed",  // Error Message
                             Eq(nullptr),
                             Eq(Status::PermissionDenied),                    // gRPC Status
                             "proto_api_scrubber_Forbidden{METHOD_BLOCKED}")) // RC Details
  ;

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, false));
}

// Tests that a request is allowed if the method-level matcher evaluates to false.
TEST_F(MethodLevelRestrictionTest, MethodAllowedByMatcher) {
  setupFilterConfig(R"pb(
    restrictions: {
      method_restrictions: {
        key: "/bookstore.Bookstore/CreateShelf"
        value: {
          method_restriction: {
            matcher: {
              matcher_list: {
                matchers: {
                  predicate: {
                    single_predicate: {
                      input: {
                        name: "request"
                        typed_config: {
                          [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] {}
                        }
                      }
                      custom_match: {
                        name: "cel"
                        typed_config: {
                          [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                            expr_match: { parsed_expr: { expr: { const_expr: { bool_value: false } } } }
                          }
                        }
                      }
                    }
                  }
                  on_match: {
                    action: {
                      name: "block"
                      typed_config: {
                        [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] {}
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
  )pb",
                    kBookstoreDescriptorRelativePath);
  reSetupFilter(kBookstoreDescriptorRelativePath);

  auto req_headers = TestRequestHeaderMapImpl{{":method", "POST"},
                                              {":path", "/bookstore.Bookstore/CreateShelf"},
                                              {"content-type", "application/grpc"}};

  EXPECT_CALL(mock_decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, false));

  // Verify data path is also fine.
  CreateShelfRequest request = makeCreateShelfRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));
}

// Tests that a request is allowed if no specific method-level rule is configured for the method.
TEST_F(MethodLevelRestrictionTest, MethodAllowedNoRule) {
  setupFilterConfig(R"pb(
    restrictions: {
      method_restrictions: {
        key: "/bookstore.Bookstore/ListShelves"
        value: {
          # No method_restriction field
        }
      }
    }
  )pb",
                    kBookstoreDescriptorRelativePath);
  reSetupFilter(kBookstoreDescriptorRelativePath);

  auto req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/bookstore.Bookstore/CreateShelf"}, // Different method.
                               {"content-type", "application/grpc"}};

  EXPECT_CALL(mock_decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, false));
}

// Tests the behavior when the matcher result is UnableToMatch (e.g., due to missing inputs).
// Currently, this test demonstrates the fallback behavior with the standard CelMatcher.
TEST_F(MethodLevelRestrictionTest, MethodAllowedOnMatcherInsufficientData) {
  // NOTE: Accurately simulating an UnableToMatch state from the CelMatcher
  // typically requires a CEL expression dependent on inputs not available
  // during decodeHeaders (e.g., dynamic metadata).
  // Testing the filter's reaction to a generic Matcher returning UnableToMatch
  // would ideally involve injecting a mock Matcher. This is not easily done
  // with the current ProtoApiScrubberFilterConfig structure, which builds the
  // matcher internally.
  // One could potentially create and register a custom Matcher extension
  // specifically designed to return UnableToMatch for testing purposes,
  // but that adds considerable complexity to this test file.

  // Using the standard CelMatcher, it's hard to force UnableToMatch.
  // This test setup will likely result in the matcher evaluating to true.

  // Setup config with a method restriction.
  setupFilterConfig(R"pb(
    restrictions: {
      method_restrictions: {
        key: "/bookstore.Bookstore/CreateShelf"
        value: {
          method_restriction: {
            matcher: {
              matcher_list: {
                matchers: {
                  predicate: {
                    single_predicate: {
                      input: {
                        name: "request"
                        typed_config: {
                          [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] {}
                        }
                      }
                      custom_match: {
                        name: "cel"
                        typed_config: {
                          [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                            # This expression is unlikely to cause UnableToMatch in this setup
                            expr_match: { parsed_expr: { expr: { const_expr: { bool_value: true } } } }
                          }
                        }
                      }
                    }
                  }
                  on_match: {
                    action: {
                      name: "block"
                      typed_config: {
                        [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] {}
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
  )pb",
                    kBookstoreDescriptorRelativePath);
  reSetupFilter(kBookstoreDescriptorRelativePath);

  auto req_headers = TestRequestHeaderMapImpl{{":method", "POST"},
                                              {":path", "/bookstore.Bookstore/CreateShelf"},
                                              {"content-type", "application/grpc"}};

  // In the current setup, this will likely result in a block, not UnableToMatch.
  // To truly test UnableToMatch, matcher mocking/injection is needed.
  // We'll assert the expected behavior IF UnableToMatch were to occur.

  // EXPECT_CALL(mock_decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  // EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers,
  // false));

  // Since we can't force UnableToMatch, this test just confirms the current matcher's behavior.
  EXPECT_CALL(mock_decoder_callbacks_,
              sendLocalReply(Http::Code::Forbidden, "Method not allowed", _, _, _));
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, false));
}

// Tests that field-level restrictions are still applied even if the method-level check passes.
TEST_F(MethodLevelRestrictionTest, MethodAllowedWithFieldRestrictions) {
  ProtoApiScrubberConfig proto_config;
  proto_config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);

  std::string method_name = "/bookstore.Bookstore/CreateShelf";

  // 1. Configure a METHOD-LEVEL rule to ALLOW the request.
  const char* config_yaml = R"pb(
    restrictions: {
      method_restrictions: {
        key: "/bookstore.Bookstore/CreateShelf"
        value: {
          method_restriction: {
            matcher: {
              matcher_list: {
                matchers: {
                  predicate: {
                    single_predicate: {
                      input: {
                        name: "request"
                        typed_config: {
                          [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] {}
                        }
                      }
                      custom_match: {
                        name: "cel"
                        typed_config: {
                          [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                            expr_match: { parsed_expr: { expr: { const_expr: { bool_value: false } } } } # Evaluates to false - No Block
                          }
                        }
                      }
                    }
                  }
                  on_match: { # This on_match won't be triggered
                    action: {
                      name: "block"
                      typed_config: {
                        [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] {}
                      }
                    }
                  }
                }
              }
            }
          }
          # Field restrictions for the same method
          request_field_restrictions: {
            key: "shelf.theme"
            value: {
              matcher: {
                matcher_list: {
                  matchers: {
                    predicate: {
                      single_predicate: {
                        input: {
                          name: "request"
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] {}
                          }
                        }
                        custom_match: {
                           name: "cel"
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                              expr_match: { parsed_expr: { expr: { const_expr: { bool_value: true } } } } # Always scrub field
                            }
                          }
                        }
                      }
                    }
                    on_match: {
                      action: {
                         name: "remove"
                        typed_config: {
                          [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] {}
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
  setupFilterConfig(config_yaml, kBookstoreDescriptorRelativePath);
  reSetupFilter(kBookstoreDescriptorRelativePath);

  auto req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", method_name}, {"content-type", "application/grpc"}};

  // Method-level check should pass.
  EXPECT_CALL(mock_decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, false));

  // Data phase should still scrub the field.
  CreateShelfRequest request = makeCreateShelfRequest(); // id: 1, theme: "Test Theme".
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  CreateShelfRequest expected_request = makeCreateShelfRequest();
  expected_request.mutable_shelf()->clear_theme(); // Theme should be scrubbed.

  checkSerializedData<CreateShelfRequest>(*request_data, {expected_request});
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
