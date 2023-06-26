#include "source/extensions/filters/http/grpc_field_extraction/filter.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"

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

class MockExtractor : public Extractor {
public:
  MOCK_METHOD(absl::Status, ProcessRequest, (google::protobuf::field_extraction::MessageData&));

  MOCK_METHOD(const ExtractionResult&, GetResult, (), (const));
};

class MockExtractorFactory : public ExtractorFactory {
public:
  MOCK_METHOD(
      ExtractorPtr, CreateExtractor,
      (TypeFinder, absl::string_view,
       const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&),
      (const));
};

class FilterTest : public ::testing::Test {
protected:
  FilterTest() : api_(Api::createApiForTest()) {}

  void SetUp() override {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
extractions_by_method: {
  key: "/apikeys.ApiKeys/CreateApiKey"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
        request_header: "dest-header"
      }
    }
  }
}
    )pb",
                                                      &proto_config_));
    proto_config_.set_proto_descriptor_bin(api_->fileSystem().fileReadToEnd(
        TestEnvironment::runfilesPath("test/proto/apikeys.descriptor")));
    ON_CALL(mock_decoder_callbacks_, decoderBufferLimit())
        .WillByDefault(testing::Return(UINT32_MAX));
    filter_config_ = std::make_unique<FilterConfig>(proto_config_, mock_extractor_factory_);
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

  testing::NiceMock<MockExtractorFactory> mock_extractor_factory_;
  testing::NiceMock<MockExtractor>* mock_extractor_;
  testing::NiceMock<MockStreamDecoderFilterCallbacks> mock_decoder_callbacks_;

  std::unique_ptr<Filter> filter_;
};

Envoy::Buffer::InstancePtr CreateApiKeyRequestEnvoyBuffer() {
  CreateApiKeyRequest request;
  Protobuf::TextFormat::ParseFromString(R"pb(
      parent: "project-id"
      key { display_name: "my-api-key" }
    )pb",
                                        &request);
  return Envoy::Grpc::Common::serializeToGrpcFrame(request);
}

void checkProtoStruct(absl::string_view got_encoded, absl::string_view wanted_in_pbtext) {
  std::string decoded;
  ASSERT(absl::Base64Unescape(got_encoded, &decoded));
  google::protobuf::Struct got, wanted;
  ASSERT(got.ParseFromString(decoded));
  Protobuf::TextFormat::ParseFromString(wanted_in_pbtext, &wanted);
  EXPECT_TRUE(TestUtility::protoEqual(got, wanted, true));
}

TEST_F(FilterTest, SunnyPath) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "application/grpc"}};
  ExtractionResult result;
  EXPECT_CALL(mock_extractor_factory_, CreateExtractor(_, _, _))
      .WillOnce(Invoke([this, &result](TypeFinder, absl::string_view request_type_url,
                                       const FieldExtractions& field_extractions) {
        EXPECT_EQ(request_type_url, "type.googleapis.com/apikeys.CreateApiKeyRequest");
        auto& extracted = result.req_fields.emplace_back();
        extracted.field_path = "parent";
        extracted.destination = &field_extractions.request_field_extractions().at("parent");
        extracted.values.emplace_back() = "project-id";
        auto ret = std::make_unique<testing::NiceMock<MockExtractor>>();
        mock_extractor_ = ret.get();
        return ret;
      }));

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(req_headers, true));

  EXPECT_CALL(*mock_extractor_, ProcessRequest(_)).WillOnce(testing::Return(absl::OkStatus()));
  EXPECT_CALL(*mock_extractor_, GetResult()).WillOnce(testing::ReturnRef(result));

  Envoy::Buffer::InstancePtr request_data = CreateApiKeyRequestEnvoyBuffer();
  Envoy::Buffer::InstancePtr request_data_old = CreateApiKeyRequestEnvoyBuffer();
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(*request_data, true));
  EXPECT_TRUE(TestUtility::buffersEqual(*request_data, *request_data_old));

  const auto injected_header = req_headers.get(Envoy::Http::LowerCaseString("dest-header"));
  ASSERT(!injected_header.empty());
  checkProtoStruct(injected_header[0]->value().getStringView(), R"pb(fields {
  key: "parent"
  value {
    list_value {
      values {
        string_value: "project-id"
      }
    }
  }
}
)pb");
}
TEST_F(FilterTest, MultipleDecodeData) {}
TEST_F(FilterTest, SunnyPathMultipleHeaders) {}
TEST_F(FilterTest, StreamingRequest) {}

using FilterTestPassThrough = FilterTest;
TEST_F(FilterTestPassThrough, NotGrpc) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"},
                               {":path", "/apikeys.ApiKeys/CreateApiKey"},
                               {"content-type", "not-grpc"}};

  EXPECT_CALL(mock_extractor_factory_, CreateExtractor(_, _, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
}

TEST_F(FilterTestPassThrough, PathNotExist) {
  TestRequestHeaderMapImpl req_headers =
      TestRequestHeaderMapImpl{{":method", "POST"}, {"content-type", "application/grpc"}};

  EXPECT_CALL(mock_extractor_factory_, CreateExtractor(_, _, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
}

TEST_F(FilterTestPassThrough, UnconfiguredRequest) {
  TestRequestHeaderMapImpl req_headers = TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/UnconfiguredRequest"}, {"content-type", "application/grpc"}};

  EXPECT_CALL(mock_extractor_factory_, CreateExtractor(_, _, _)).Times(0);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
}

using FilterTestRejected = FilterTest;
TEST_F(FilterTestRejected, FailedBufferConversion) {}

TEST_F(FilterTestRejected, FailedFieldExtraction) {}

TEST_F(FilterTestRejected, NotEnoughData) {}

} // namespace

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction