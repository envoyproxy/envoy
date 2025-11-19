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
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::checkSerializedData;
using ::Envoy::Http::MockStreamDecoderFilterCallbacks;
using ::Envoy::Http::MockStreamEncoderFilterCallbacks;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Protobuf::Struct;
using ::testing::Eq;

inline constexpr const char kApiKeysDescriptorRelativePath[] = "test/proto/apikeys.descriptor";
inline constexpr char kRemoveFieldActionType[] =
    "type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction";

class ProtoApiScrubberFilterTest : public ::testing::Test {
protected:
  ProtoApiScrubberFilterTest() : api_(Api::createApiForTest()) { setup(); }

  // Helper Enum for clarity
  enum class FieldType { Request, Response };

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
  absl::Status reloadFilter(ProtoApiScrubberConfig& config) {
    // Ensure descriptors are present
    if (!config.has_descriptor_set()) {
      auto content_or = api_->fileSystem().fileReadToEnd(
          Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath));
      RETURN_IF_NOT_OK(content_or.status());

      *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
          std::move(content_or.value());
    }

    // Create new Config Object
    auto config_or_status = ProtoApiScrubberFilterConfig::create(config, mock_factory_context_);
    RETURN_IF_NOT_OK(config_or_status.status());

    // Reset the filter config instance
    filter_config_ = config_or_status.value();

    // Reset the filter instance
    setupFilter();

    return absl::OkStatus();
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
}

TEST_F(ProtoApiScrubberFilterTest, UnknownGrpcMethod) {
  ProtoApiScrubberConfig config;
  ASSERT_TRUE(reloadFilter(config).ok());

  // Prepare request
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

  // Post-check: Verify scrubbing happened
  CreateApiKeyRequest expected_scrubbed_request = makeCreateApiKeyRequest();
  expected_scrubbed_request.mutable_key()->mutable_update_time()->clear_seconds();

  checkSerializedData<CreateApiKeyRequest>(*request_data, {expected_scrubbed_request});
}

} // namespace

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
