#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"
#include "source/extensions/filters/http/grpc_field_extraction/filter.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {
namespace {

using ::apikeys::ApiKey;
using ::apikeys::CreateApiKeyRequest;
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions;
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig;
using ::Envoy::Http::MockStreamDecoderFilterCallbacks;
using ::Envoy::Http::MockStreamEncoderFilterCallbacks;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Server::Configuration::FactoryContext;
using ::testing::Eq;

constexpr absl::string_view expected_metadata = R"pb(
fields {
  key: "parent"
  value {
    list_value {
      values {
        string_value: "project-id"
      }
    }
  }
}
)pb";

class FilterTest : public ::testing::Test {
protected:
  FilterTest() : api_(Api::createApiForTest()) {}

  void SetUp() override {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(protoConfig(), &proto_config_));
    *proto_config_.mutable_descriptor_set()->mutable_inline_bytes() =
        api_->fileSystem().fileReadToEnd(
            TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"));
    ON_CALL(mock_decoder_callbacks_, decoderBufferLimit())
        .WillByDefault(testing::Return(UINT32_MAX));
    filter_config_ = std::make_unique<FilterConfig>(proto_config_, *extractor_factory_, *api_);
    filter_ = std::make_unique<Filter>(*filter_config_);
    filter_->setDecoderFilterCallbacks(mock_decoder_callbacks_);
  }

  void TearDown() override {
    // Test onDestroy doesn't crash.
    filter_->onDestroy();
  }

  virtual std::string protoConfig() {
    return R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
      }
    }
  }
})pb";
  }

  Api::ApiPtr api_;
  GrpcFieldExtractionConfig proto_config_;
  std::unique_ptr<FilterConfig> filter_config_;

  std::unique_ptr<ExtractorFactory> extractor_factory_ = std::make_unique<ExtractorFactoryImpl>();
  testing::NiceMock<MockStreamDecoderFilterCallbacks> mock_decoder_callbacks_;

  std::unique_ptr<Filter> filter_;
};

CreateApiKeyRequest MakeCreateApiKeyRequest(absl::string_view pb = R"pb(
      parent: "project-id"
    )pb") {
  CreateApiKeyRequest request;
  Protobuf::TextFormat::ParseFromString(pb, &request);
  return request;
}

void checkProtoStruct(ProtobufWkt::Struct got, absl::string_view expected_in_pbtext) {
  google::protobuf::Struct expected;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(expected_in_pbtext, &expected));
  EXPECT_TRUE(TestUtility::protoEqual(got, expected, true)) << "got:\n"
                                                            << got.DebugString() << "expected:\n"
                                                            << expected_in_pbtext;
}

using FilterTestUnaryExtractSuccess = FilterTest;
TEST_F(FilterTestUnaryExtractSuccess, UnarySingleBuffer) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  CreateApiKeyRequest request = MakeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns, const ProtobufWkt::Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.grpc_field_extraction");
        checkProtoStruct(new_dynamic_metadata, expected_metadata);
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  // No data modification.
  checkSerializedData<CreateApiKeyRequest>(*request_data, {request});
}

TEST_F(FilterTestUnaryExtractSuccess, EmptyFields) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  CreateApiKeyRequest request = MakeCreateApiKeyRequest("");
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns, const ProtobufWkt::Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.grpc_field_extraction");
        checkProtoStruct(new_dynamic_metadata, R"pb(
fields {
  key: "parent"
  value {
    list_value {
    }
  }
})pb");
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  // No data modification.
  checkSerializedData<CreateApiKeyRequest>(*request_data, {request});
}

TEST_F(FilterTestUnaryExtractSuccess, UnaryMultipeBuffers) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  constexpr uint start_data_size = 3;
  constexpr uint middle_data_size = 4;

  CreateApiKeyRequest request = MakeCreateApiKeyRequest();
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
      .WillOnce(Invoke([](const std::string& ns, const ProtobufWkt::Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.grpc_field_extraction");
        checkProtoStruct(new_dynamic_metadata, expected_metadata);
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(end_request_data, true));

  // Inject data back and no data modification.
  checkSerializedData<CreateApiKeyRequest>(end_request_data, {request});
}

class FilterTestStreamingExtractSuccess : public FilterTest {
protected:
  FilterTestStreamingExtractSuccess() : FilterTest() {}

  std::string protoConfig() override {
    return R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKeyInStream"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
      }
    }
  }
}
    )pb";
  }
};

TEST_F(FilterTestStreamingExtractSuccess, StreamingMultipleMessageSingleBuffer) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKeyInStream"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));
  CreateApiKeyRequest request1 = MakeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data1 = Envoy::Grpc::Common::serializeToGrpcFrame(request1);
  CreateApiKeyRequest request2 = MakeCreateApiKeyRequest(
      R"pb(
      parent: "from-req2"
)pb");
  Envoy::Buffer::InstancePtr request_data2 = Envoy::Grpc::Common::serializeToGrpcFrame(request2);
  CreateApiKeyRequest request3 = MakeCreateApiKeyRequest(
      R"pb(
      parent: "from-req3"
)pb");
  Envoy::Buffer::InstancePtr request_data3 = Envoy::Grpc::Common::serializeToGrpcFrame(request3);

  // Split into multiple buffers.
  Envoy::Buffer::OwnedImpl request_data;
  request_data.move(*request_data1);
  request_data.move(*request_data2);
  request_data.move(*request_data3);
  EXPECT_EQ(request_data1->length(), 0);
  EXPECT_EQ(request_data2->length(), 0);
  EXPECT_EQ(request_data3->length(), 0);

  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns, const ProtobufWkt::Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.grpc_field_extraction");
        checkProtoStruct(new_dynamic_metadata, expected_metadata);
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(request_data, false));

  // Inject data back and no data modification.
  checkSerializedData<CreateApiKeyRequest>(request_data,
                                                            {request1, request2, request3});

  // No op for the following messages.
  CreateApiKeyRequest request4 = MakeCreateApiKeyRequest(
      R"pb(
      parent: "from-req4"
)pb");
  Envoy::Buffer::InstancePtr request_data4 = Envoy::Grpc::Common::serializeToGrpcFrame(request4);
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data4, true));
  checkSerializedData<CreateApiKeyRequest>(*request_data4, {request4});
}

// Only test several field types. For all the primitive types, they are covered in
class FilterTestFieldTypes : public FilterTest {
protected:
  FilterTestFieldTypes() : FilterTest() {}

  std::string protoConfig() override {
    return R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "key.type_repeated_string"
      value: {
      }
    }
  }
}
    )pb";
  }
};

TEST_F(FilterTestFieldTypes, NestedRepeatedString) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, false));

  CreateApiKeyRequest request = MakeCreateApiKeyRequest(
      R"pb(
key: {
type_repeated_string: "str1"
type_repeated_string: "str2"
}
)pb");
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns, const ProtobufWkt::Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.grpc_field_extraction");
        checkProtoStruct(new_dynamic_metadata, R"pb(
fields {
  key: "key.type_repeated_string"
  value {
    list_value {
      values {
        string_value: "str1"
      }
      values {
        string_value: "str2"
      }
    }
  }
}
)pb");
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));

  // No data modification.
  checkSerializedData<CreateApiKeyRequest>(*request_data, {request});
}

using FilterTestExtractFailRejected = FilterTest;
TEST_F(FilterTestExtractFailRejected, BufferLimitedExceeded) {
  ON_CALL(mock_decoder_callbacks_, decoderBufferLimit()).WillByDefault(testing::Return(0));

  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  CreateApiKeyRequest request = MakeCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data = Envoy::Grpc::Common::serializeToGrpcFrame(request);

  EXPECT_CALL(mock_decoder_callbacks_,
              sendLocalReply(
                  Http::Code::BadRequest, "Rejected because internal buffer limits are exceeded.",
                  Eq(nullptr), Eq(Envoy::Grpc::Status::FailedPrecondition),
                  "grpc_field_extraction_FAILED_PRECONDITION{REQUEST_BUFFER_CONVERSION_FAIL}"));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(*request_data, true));
}

TEST_F(FilterTestExtractFailRejected, NotEnoughData) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, false));

  Envoy::Buffer::OwnedImpl empty;

  EXPECT_CALL(mock_decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Did not receive enough data to form a message.", Eq(nullptr),
                             Eq(Envoy::Grpc::Status::InvalidArgument),
                             "grpc_field_extraction_INVALID_ARGUMENT{REQUEST_OUT_OF_DATA}"));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(empty, true));
}

TEST_F(FilterTestExtractFailRejected, MisformedGrpcPath) {
  ON_CALL(mock_decoder_callbacks_, decoderBufferLimit()).WillByDefault(testing::Return(0));

  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/misformatted"}, {"content-type", "application/grpc"}};
  EXPECT_CALL(mock_decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             ":path `/misformatted` should be in form of `/package.Service/method`",
                             Eq(nullptr), Eq(Envoy::Grpc::Status::InvalidArgument),
                             "grpc_field_extraction_INVALID_ARGUMENT{BAD_REQUEST}"));

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, false));
}

using FilterTestPassThrough = FilterTest;
TEST_F(FilterTestPassThrough, NotGrpc) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "not-grpc"}};

  // Pass through headers directly.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, false));
}

TEST_F(FilterTestPassThrough, PathNotExist) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"}, {"content-type", "application/grpc"}};

  // Pass through headers directly.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, false));
}

TEST_F(FilterTestPassThrough, UnconfiguredRequest) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/pkg.svc/UnconfiguredRequest"},
                               {"content-type", "application/grpc"}};

  // Pass through headers directly.
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, false));
}

} // namespace

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
