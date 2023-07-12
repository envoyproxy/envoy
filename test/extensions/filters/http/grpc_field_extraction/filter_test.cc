#include "source/extensions/filters/http/grpc_field_extraction/filter.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {
namespace {

using ::apikeys::CreateApiKeyRequest;
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions;
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig;
using ::Envoy::Http::MockStreamDecoderFilterCallbacks;
using ::Envoy::Http::MockStreamEncoderFilterCallbacks;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Server::Configuration::FactoryContext;

constexpr absl::string_view expected_metadata = R"pb(fields {
  key: "key.name"
  value {
    list_value {
      values {
        string_value: "key-name"
      }
    }
  }
}
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

constexpr absl::string_view  proto_config_pbtext = R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
      }
    }
    request_field_extractions: {
      key: "key.name"
      value: {
      }
    }
  }
}
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKeyInStream"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
      }
    }
    request_field_extractions: {
      key: "key.name"
      value: {
      }
    }
  }
}
    )pb";
class FilterTest : public ::testing::Test {
protected:
  FilterTest() : api_(Api::createApiForTest()) {}

  void SetUp() override {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(proto_config_pbtext,
                                                      &proto_config_));
    *proto_config_.mutable_descriptor_set()->mutable_inline_bytes() = api_->fileSystem().fileReadToEnd(
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

  Api::ApiPtr api_;
  GrpcFieldExtractionConfig proto_config_;
  std::unique_ptr<FilterConfig> filter_config_;

  std::unique_ptr<ExtractorFactory> extractor_factory_ = std::make_unique<ExtractorFactoryImpl>();
  testing::NiceMock<MockStreamDecoderFilterCallbacks> mock_decoder_callbacks_;

  std::unique_ptr<Filter> filter_;
};

Envoy::Buffer::InstancePtr CreateApiKeyRequestEnvoyBuffer(absl::string_view pb = R"pb(
      parent: "project-id"
      key { name: "key-name" }
    )pb") {
  CreateApiKeyRequest request;
  Protobuf::TextFormat::ParseFromString(pb,
                                        &request);
  return Envoy::Grpc::Common::serializeToGrpcFrame(request);
}

void checkProtoStruct(Protobuf::Struct got, absl::string_view expected_in_pbtext) {
  google::protobuf::Struct  expected;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(expected_in_pbtext, &expected));
  EXPECT_TRUE(TestUtility::protoEqual(got, expected, true)) << "got:\n" <<got.DebugString() << "expected:\n" << expected_in_pbtext ;
}

using FilterTestExtractSuccess = FilterTest;
TEST_F(FilterTestExtractSuccess, UnarySingleDecodeData) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  Envoy::Buffer::InstancePtr request_data = CreateApiKeyRequestEnvoyBuffer();
  Envoy::Buffer::InstancePtr original_request_data = CreateApiKeyRequestEnvoyBuffer();
  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns,
                                           const ProtobufWkt::Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.grpc_field_extraction");
        checkProtoStruct(new_dynamic_metadata, expected_metadata);
      }));

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));
  EXPECT_TRUE(TestUtility::buffersEqual(*request_data, *original_request_data));
}

TEST_F(FilterTestExtractSuccess, UnaryMultipeDecodeDatas) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));


  constexpr uint start_data_size = 3;
  constexpr uint middle_data_size = 4;

  Envoy::Buffer::InstancePtr request_data = CreateApiKeyRequestEnvoyBuffer();

  // Split into multiple buffers.
  Envoy::Buffer::OwnedImpl start_request_data;
  Envoy::Buffer::OwnedImpl middle_request_data;
  Envoy::Buffer::OwnedImpl end_request_data;
  start_request_data.move(*request_data, start_data_size);
  middle_request_data.move(*request_data, middle_data_size);
  end_request_data.move(*request_data);
  EXPECT_EQ(request_data->length(), 0);


  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(start_request_data, false ));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(middle_request_data, false));
    EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns,
                                           const ProtobufWkt::Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.grpc_field_extraction");
        checkProtoStruct(new_dynamic_metadata, expected_metadata);
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(end_request_data, false));
}

TEST_F(FilterTestExtractSuccess, Streaming) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  Envoy::Buffer::InstancePtr request_1= CreateApiKeyRequestEnvoyBuffer();
  Envoy::Buffer::InstancePtr request_2= CreateApiKeyRequestEnvoyBuffer(R"pb(
      parent: "project-id-2"
    )pb");
  Envoy::Buffer::OwnedImpl request_in;
  request_in.move(*request_1);
  request_in.move(*request_2);
  EXPECT_EQ(request_1->length(), 0);
  EXPECT_EQ(request_2->length(), 0);
    EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([](const std::string& ns,
                                           const ProtobufWkt::Struct& new_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.grpc_field_extraction");
        checkProtoStruct(new_dynamic_metadata,expected_metadata);
      }));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(request_in, true));
}

using FilterTestExtractFailRejected = FilterTest;
TEST_F(FilterTestExtractFailRejected, FailedBufferConversion) {

}

TEST_F(FilterTestExtractFailRejected, FailedFieldExtraction) {

}

TEST_F(FilterTestExtractFailRejected, NotEnoughData) {}

using FilterTestPassThrough = FilterTest;
TEST_F(FilterTestPassThrough, NotGrpc) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "not-grpc"}};

  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
    Envoy::Buffer::InstancePtr request_data = CreateApiKeyRequestEnvoyBuffer();
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));
}

TEST_F(FilterTestPassThrough, PathNotExist) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"}, {"content-type", "application/grpc"}};

  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
  Envoy::Buffer::InstancePtr request_data = CreateApiKeyRequestEnvoyBuffer();
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));
}

TEST_F(FilterTestPassThrough, UnconfiguredRequest) {
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/pkg.svc/UnconfiguredRequest"}, {"content-type", "application/grpc"}};

  EXPECT_CALL(mock_decoder_callbacks_.stream_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
    Envoy::Buffer::InstancePtr request_data = CreateApiKeyRequestEnvoyBuffer();
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));
}


} // namespace

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction