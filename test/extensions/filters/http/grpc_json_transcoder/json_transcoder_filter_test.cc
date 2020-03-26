#include <fstream>
#include <functional>

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;

using Envoy::Protobuf::MethodDescriptor;

using Envoy::Protobuf::FileDescriptorProto;
using Envoy::Protobuf::FileDescriptorSet;
using Envoy::Protobuf::util::MessageDifferencer;
using Envoy::ProtobufUtil::error::Code;
using google::api::HttpRule;
using google::grpc::transcoding::Transcoder;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {
namespace {

class GrpcJsonTranscoderFilterTestBase {
protected:
  GrpcJsonTranscoderFilterTestBase() : api_(Api::createApiForTest()) {}
  ~GrpcJsonTranscoderFilterTestBase() {
    TestEnvironment::removePath(TestEnvironment::temporaryPath("envoy_test/proto.descriptor"));
  }

  Api::ApiPtr api_;
};

class GrpcJsonTranscoderConfigTest : public testing::Test, public GrpcJsonTranscoderFilterTestBase {
protected:
  const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder
  getProtoConfig(const std::string& descriptor_path, const std::string& service_name,
                 bool match_incoming_request_route = false,
                 const std::vector<std::string>& ignored_query_parameters = {}) {
    const std::string json_string = "{\"proto_descriptor\": \"" + descriptor_path +
                                    "\",\"services\": [\"" + service_name + "\"]}";
    envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config;
    TestUtility::loadFromJson(json_string, proto_config);
    proto_config.set_match_incoming_request_route(match_incoming_request_route);
    for (const auto& query_param : ignored_query_parameters) {
      proto_config.add_ignored_query_parameters(query_param);
    }

    return proto_config;
  }

  std::string makeProtoDescriptor(std::function<void(FileDescriptorSet&)> process) {
    FileDescriptorSet descriptor_set;
    descriptor_set.ParseFromString(api_->fileSystem().fileReadToEnd(
        TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));

    process(descriptor_set);

    TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
    std::string path = TestEnvironment::temporaryPath("envoy_test/proto.descriptor");
    std::ofstream file(path, std::ios::binary);
    descriptor_set.SerializeToOstream(&file);

    return path;
  }

  void setGetBookHttpRule(FileDescriptorSet& descriptor_set, const HttpRule& http_rule) {
    for (auto& file : *descriptor_set.mutable_file()) {
      for (auto& service : *file.mutable_service()) {
        for (auto& method : *service.mutable_method()) {
          if (method.name() == "GetBook") {
            method.mutable_options()->MutableExtension(google::api::http)->MergeFrom(http_rule);
            return;
          }
        }
      }
    }
  }

  void stripImports(FileDescriptorSet& descriptor_set, const std::string& file_name) {
    FileDescriptorProto file_descriptor;
    // filter down descriptor_set to only contain one proto specified as file_name but none of its
    // dependencies
    auto file_itr =
        std::find_if(descriptor_set.file().begin(), descriptor_set.file().end(),
                     [&file_name](const FileDescriptorProto& file) {
                       // return whether file.name() ends with file_name
                       return file.name().length() >= file_name.length() &&
                              0 == file.name().compare(file.name().length() - file_name.length(),
                                                       std::string::npos, file_name);
                     });
    RELEASE_ASSERT(file_itr != descriptor_set.file().end(), "");
    file_descriptor = *file_itr;

    descriptor_set.clear_file();
    descriptor_set.add_file()->Swap(&file_descriptor);
  }
};

TEST_F(GrpcJsonTranscoderConfigTest, ParseConfig) {
  EXPECT_NO_THROW(JsonTranscoderConfig config(
      getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"),
                     "bookstore.Bookstore"),
      *api_));
}

TEST_F(GrpcJsonTranscoderConfigTest, ParseConfigSkipRecalculating) {
  EXPECT_NO_THROW(JsonTranscoderConfig config(
      getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"),
                     "bookstore.Bookstore", true),
      *api_));
}

TEST_F(GrpcJsonTranscoderConfigTest, ParseBinaryConfig) {
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config;
  proto_config.set_proto_descriptor_bin(api_->fileSystem().fileReadToEnd(
      TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  proto_config.add_services("bookstore.Bookstore");
  EXPECT_NO_THROW(JsonTranscoderConfig config(proto_config, *api_));
}

TEST_F(GrpcJsonTranscoderConfigTest, UnknownService) {
  EXPECT_THROW_WITH_MESSAGE(
      JsonTranscoderConfig config(
          getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"),
                         "grpc.service.UnknownService"),
          *api_),
      EnvoyException,
      "transcoding_filter: Could not find 'grpc.service.UnknownService' in the proto descriptor");
}

TEST_F(GrpcJsonTranscoderConfigTest, IncompleteProto) {
  EXPECT_THROW_WITH_MESSAGE(
      JsonTranscoderConfig config(getProtoConfig(makeProtoDescriptor([&](FileDescriptorSet& pb) {
                                                   stripImports(pb, "test/proto/bookstore.proto");
                                                 }),
                                                 "bookstore.Bookstore"),
                                  *api_),
      EnvoyException, "transcoding_filter: Unable to build proto descriptor pool");
}

TEST_F(GrpcJsonTranscoderConfigTest, NonProto) {
  EXPECT_THROW_WITH_MESSAGE(
      JsonTranscoderConfig config(
          getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.proto"),
                         "grpc.service.UnknownService"),
          *api_),
      EnvoyException, "transcoding_filter: Unable to parse proto descriptor");
}

TEST_F(GrpcJsonTranscoderConfigTest, NonBinaryProto) {
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config;
  proto_config.set_proto_descriptor_bin("This is invalid proto");
  proto_config.add_services("bookstore.Bookstore");
  EXPECT_THROW_WITH_MESSAGE(JsonTranscoderConfig config(proto_config, *api_), EnvoyException,
                            "transcoding_filter: Unable to parse proto descriptor");
}

TEST_F(GrpcJsonTranscoderConfigTest, InvalidHttpTemplate) {
  HttpRule http_rule;
  http_rule.set_get("/book/{");
  EXPECT_THROW_WITH_MESSAGE(
      JsonTranscoderConfig config(getProtoConfig(makeProtoDescriptor([&](FileDescriptorSet& pb) {
                                                   setGetBookHttpRule(pb, http_rule);
                                                 }),
                                                 "bookstore.Bookstore"),
                                  *api_),
      EnvoyException,
      "transcoding_filter: Cannot register 'bookstore.Bookstore.GetBook' to path matcher");
}

TEST_F(GrpcJsonTranscoderConfigTest, CreateTranscoder) {
  JsonTranscoderConfig config(
      getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"),
                     "bookstore.Bookstore"),
      *api_);

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/shelves"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  MethodInfoSharedPtr method_info;
  const auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_info);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder);
  EXPECT_EQ("bookstore.Bookstore.ListShelves", method_info->descriptor_->full_name());
}

TEST_F(GrpcJsonTranscoderConfigTest, CreateTranscoderAutoMap) {
  auto proto_config = getProtoConfig(
      TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"), "bookstore.Bookstore");
  proto_config.set_auto_mapping(true);

  JsonTranscoderConfig config(proto_config, *api_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/DeleteShelf"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  MethodInfoSharedPtr method_info;
  const auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_info);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder);
  EXPECT_EQ("bookstore.Bookstore.DeleteShelf", method_info->descriptor_->full_name());
}

TEST_F(GrpcJsonTranscoderConfigTest, InvalidQueryParameter) {
  JsonTranscoderConfig config(
      getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"),
                     "bookstore.Bookstore"),
      *api_);

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/shelves?foo=bar"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  MethodInfoSharedPtr method_info;
  const auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_info);

  EXPECT_EQ(Code::INVALID_ARGUMENT, status.error_code());
  EXPECT_EQ("Could not find field \"foo\" in the type \"google.protobuf.Empty\".",
            status.error_message());
  EXPECT_FALSE(transcoder);
}

TEST_F(GrpcJsonTranscoderConfigTest, UnknownQueryParameterIsIgnored) {
  auto proto_config = getProtoConfig(
      TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"), "bookstore.Bookstore");
  proto_config.set_ignore_unknown_query_parameters(true);
  JsonTranscoderConfig config(proto_config, *api_);

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/shelves?foo=bar"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  MethodInfoSharedPtr method_info;
  const auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_info);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder);
}

TEST_F(GrpcJsonTranscoderConfigTest, IgnoredQueryParameter) {
  std::vector<std::string> ignored_query_parameters = {"key"};
  JsonTranscoderConfig config(
      getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"),
                     "bookstore.Bookstore", false, ignored_query_parameters),
      *api_);

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/shelves?key=API_KEY"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  MethodInfoSharedPtr method_info;
  const auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_info);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder);
  EXPECT_EQ("bookstore.Bookstore.ListShelves", method_info->descriptor_->full_name());
}

TEST_F(GrpcJsonTranscoderConfigTest, InvalidVariableBinding) {
  HttpRule http_rule;
  http_rule.set_get("/book/{b}");
  JsonTranscoderConfig config(getProtoConfig(makeProtoDescriptor([&](FileDescriptorSet& pb) {
                                               setGetBookHttpRule(pb, http_rule);
                                             }),
                                             "bookstore.Bookstore"),
                              *api_);

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/book/1"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  MethodInfoSharedPtr method_info;
  const auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_info);

  EXPECT_EQ(Code::INVALID_ARGUMENT, status.error_code());
  EXPECT_EQ("Could not find field \"b\" in the type \"bookstore.GetBookRequest\".",
            status.error_message());
  EXPECT_FALSE(transcoder);
}

class GrpcJsonTranscoderFilterTest : public testing::Test, public GrpcJsonTranscoderFilterTestBase {
protected:
  GrpcJsonTranscoderFilterTest(
      envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config =
          bookstoreProtoConfig())
      : config_(proto_config, *api_), filter_(config_) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  static const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder
  bookstoreProtoConfig() {
    const std::string json_string = "{\"proto_descriptor\": \"" + bookstoreDescriptorPath() +
                                    "\",\"services\": [\"bookstore.Bookstore\"]}";
    envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config;
    TestUtility::loadFromJson(json_string, proto_config);
    return proto_config;
  }

  static const std::string bookstoreDescriptorPath() {
    return TestEnvironment::runfilesPath("test/proto/bookstore.descriptor");
  }

  // TODO(lizan): Add a mock of JsonTranscoderConfig and test more error cases.
  JsonTranscoderConfig config_;
  JsonTranscoderFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(GrpcJsonTranscoderFilterTest, NoTranscoding) {
  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                                 {":method", "POST"},
                                                 {":path", "/grpc.service/UnknownGrpcMethod"}};

  Http::TestHeaderMapImpl expected_request_headers{{"content-type", "application/grpc"},
                                                   {":method", "POST"},
                                                   {":path", "/grpc.service/UnknownGrpcMethod"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache()).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(expected_request_headers, request_headers);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));

  Buffer::OwnedImpl request_data{"{}"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, false));
  EXPECT_EQ(2, request_data.length());

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};

  Http::TestHeaderMapImpl expected_response_headers{{"content-type", "application/grpc"},
                                                    {":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(expected_response_headers, response_headers);

  Buffer::OwnedImpl response_data{"{}"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_data, false));
  EXPECT_EQ(2, response_data.length());

  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  Http::TestHeaderMapImpl expected_response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(expected_response_trailers, response_trailers);
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryPost) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/shelf", request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelf", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\"}"};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, true));

  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;
  decoder.decode(request_data, frames);

  EXPECT_EQ(1, frames.size());

  bookstore::CreateShelfRequest expected_request;
  expected_request.mutable_shelf()->set_theme("Children");

  bookstore::CreateShelfRequest request;
  request.ParseFromString(frames[0].data_->toString());

  EXPECT_EQ(expected_request.ByteSize(), frames[0].length_);
  EXPECT_TRUE(MessageDifferencer::Equals(expected_request, request));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "000"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.encodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));

  bookstore::Shelf response;
  response.set_id(20);
  response.set_theme("Children");

  auto response_data = Grpc::Common::serializeToGrpcFrame(response);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_.encodeData(*response_data, false));

  std::string response_json = response_data->toString();

  EXPECT_EQ("{\"id\":\"20\",\"theme\":\"Children\"}", response_json);

  Http::TestRequestTrailerMapImpl request_trailers;

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryPostWithPackageServiceMethodPath) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/json"},
      {":method", "POST"},
      {":path", "/bookstore.Bookstore/CreateShelfWithPackageServiceAndMethod"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelfWithPackageServiceAndMethod",
            request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelfWithPackageServiceAndMethod",
            request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\"}"};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, true));

  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;
  decoder.decode(request_data, frames);

  EXPECT_EQ(1, frames.size());

  bookstore::CreateShelfRequest expected_request;
  expected_request.mutable_shelf()->set_theme("Children");

  bookstore::CreateShelfRequest request;
  request.ParseFromString(frames[0].data_->toString());

  EXPECT_EQ(expected_request.ByteSize(), frames[0].length_);
  EXPECT_TRUE(MessageDifferencer::Equals(expected_request, request));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "000"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));

  bookstore::Shelf response;
  response.set_id(20);
  response.set_theme("Children");

  auto response_data = Grpc::Common::serializeToGrpcFrame(response);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_.encodeData(*response_data, false));

  std::string response_json = response_data->toString();

  EXPECT_EQ("{\"id\":\"20\",\"theme\":\"Children\"}", response_json);

  Http::TestRequestTrailerMapImpl request_trailers;

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));
}

TEST_F(GrpcJsonTranscoderFilterTest, ForwardUnaryPostGrpc) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":method", "POST"},
      {":path", "/bookstore.Bookstore/CreateShelfWithPackageServiceAndMethod"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelfWithPackageServiceAndMethod",
            request_headers.get_(":path"));

  bookstore::CreateShelfRequest request;
  request.mutable_shelf()->set_theme("Children");

  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(*request_data, true));

  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;
  decoder.decode(*request_data, frames);

  EXPECT_EQ(1, frames.size());

  bookstore::CreateShelfRequest expected_request;
  expected_request.mutable_shelf()->set_theme("Children");

  bookstore::CreateShelfRequest forwarded_request;
  forwarded_request.ParseFromString(frames[0].data_->toString());

  EXPECT_EQ(expected_request.ByteSize(), frames[0].length_);
  EXPECT_TRUE(MessageDifferencer::Equals(expected_request, forwarded_request));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "000"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("application/grpc", response_headers.get_("content-type"));

  bookstore::Shelf expected_response;
  expected_response.set_id(20);
  expected_response.set_theme("Children");

  bookstore::Shelf response;
  response.set_id(20);
  response.set_theme("Children");

  Buffer::InstancePtr response_data = Grpc::Common::serializeToGrpcFrame(response);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(*response_data, true));

  frames.clear();
  decoder.decode(*response_data, frames);

  EXPECT_EQ(1, frames.size());

  bookstore::Shelf forwarded_response;
  forwarded_response.ParseFromString(frames[0].data_->toString());

  EXPECT_EQ(expected_response.ByteSize(), frames[0].length_);
  EXPECT_TRUE(MessageDifferencer::Equals(expected_response, forwarded_response));

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));
}

class GrpcJsonTranscoderFilterSkipRecalculatingTest : public GrpcJsonTranscoderFilterTest {
public:
  GrpcJsonTranscoderFilterSkipRecalculatingTest()
      : GrpcJsonTranscoderFilterTest(makeProtoConfig()) {}

private:
  const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder
  makeProtoConfig() {
    auto proto_config = bookstoreProtoConfig();
    proto_config.set_match_incoming_request_route(true);
    return proto_config;
  }
};

TEST_F(GrpcJsonTranscoderFilterSkipRecalculatingTest, TranscodingUnaryPostSkipRecalculate) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache()).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));

  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/shelf", request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelf", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\"}"};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, true));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryError) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelf", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\""};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool end_stream) {
        EXPECT_EQ("400", headers.Status()->value().getStringView());
        EXPECT_FALSE(end_stream);
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(request_data, true));
  EXPECT_EQ(0, request_data.length());
  EXPECT_EQ(decoder_callbacks_.details_, "grpc_json_transcode_failure{INVALID_ARGUMENT}");
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryTimeout) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelf", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\"}"};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, true));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(request_data, true));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryNotGrpcResponse) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelf", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\"}"};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, true));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(request_data, true));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryWithHttpBodyAsOutput) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/index"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/index", request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/GetIndex", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));

  google::api::HttpBody response;
  response.set_content_type("text/html");
  response.set_data("<h1>Hello, world!</h1>");

  auto response_data = Grpc::Common::serializeToGrpcFrame(response);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_.encodeData(*response_data, false));

  EXPECT_EQ(response.content_type(), response_headers.get_("content-type"));
  EXPECT_EQ(response.data(), response_data->toString());

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryWithHttpBodyAsOutputAndSplitTwoEncodeData) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/index"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/index", request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/GetIndex", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));

  google::api::HttpBody response;
  response.set_content_type("text/html");
  response.set_data("<h1>Hello, world!</h1>");

  auto response_data = Grpc::Common::serializeToGrpcFrame(response);

  // Firstly, the response data buffer is split into two parts.
  Buffer::OwnedImpl response_data_first_part;
  response_data_first_part.move(*response_data, response_data->length() / 2);

  // Secondly, we send the first part of response data to the data encoding step.
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_.encodeData(response_data_first_part, false));

  // Finally, since half of the response data buffer is moved already, here we can send the rest
  // of it to the next data encoding step.
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_.encodeData(*response_data, false));

  EXPECT_EQ(response.content_type(), response_headers.get_("content-type"));
  EXPECT_EQ(response.data(), response_data->toString());

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryPostWithHttpBody) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/postBody?arg=hi"}, {"content-type", "text/plain"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/postBody?arg=hi", request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/PostBody", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
      .Times(testing::AtLeast(1))
      .WillRepeatedly(testing::Invoke([&decoder, &frames](Buffer::Instance& data, bool end_stream) {
        EXPECT_TRUE(end_stream);
        decoder.decode(data, frames);
      }));

  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(buffer, false));
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(frames.size(), 0);
  buffer.add(" ");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(buffer, false));
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(frames.size(), 0);
  buffer.add("world!");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(buffer, true));
  EXPECT_EQ(buffer.length(), 0);
  ASSERT_EQ(frames.size(), 1);

  bookstore::EchoBodyRequest expected_request;
  expected_request.set_arg("hi");
  expected_request.mutable_nested()->mutable_content()->set_content_type("text/plain");
  expected_request.mutable_nested()->mutable_content()->set_data("hello world!");

  bookstore::EchoBodyRequest request;
  request.ParseFromString(frames[0].data_->toString());

  EXPECT_THAT(request, ProtoEq(expected_request));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingStreamPostWithHttpBody) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/streamBody?arg=hi"}, {"content-type", "text/plain"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/streamBody?arg=hi", request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/StreamBody", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
      .Times(testing::AtLeast(2))
      .WillRepeatedly(testing::Invoke([&decoder, &frames](Buffer::Instance& data, bool end_stream) {
        EXPECT_TRUE(end_stream);
        decoder.decode(data, frames);
      }));

  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(buffer, false));
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(frames.size(), 1);
  buffer.add(" ");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(buffer, false));
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(frames.size(), 2);
  buffer.add("world!");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(buffer, true));
  EXPECT_EQ(buffer.length(), 0);
  ASSERT_EQ(frames.size(), 3);

  bookstore::EchoBodyRequest expected_request;
  bookstore::EchoBodyRequest request;

  expected_request.set_arg("hi");
  expected_request.mutable_nested()->mutable_content()->set_content_type("text/plain");
  expected_request.mutable_nested()->mutable_content()->set_data("hello");
  request.ParseFromString(frames[0].data_->toString());
  EXPECT_THAT(request, ProtoEq(expected_request));

  expected_request.Clear();
  expected_request.mutable_nested()->mutable_content()->set_data(" ");
  request.ParseFromString(frames[1].data_->toString());
  EXPECT_THAT(request, ProtoEq(expected_request));

  expected_request.Clear();
  expected_request.mutable_nested()->mutable_content()->set_data("world!");
  request.ParseFromString(frames[2].data_->toString());
  EXPECT_THAT(request, ProtoEq(expected_request));
}

class GrpcJsonTranscoderFilterGrpcStatusTest : public GrpcJsonTranscoderFilterTest {
public:
  GrpcJsonTranscoderFilterGrpcStatusTest(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
          proto_config)
      : GrpcJsonTranscoderFilterTest(proto_config) {}
  GrpcJsonTranscoderFilterGrpcStatusTest() : GrpcJsonTranscoderFilterTest(makeProtoConfig()) {}

  void SetUp() override {
    EXPECT_CALL(decoder_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl request_headers{
        {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));

    Buffer::OwnedImpl request_data{R"({"theme": "Children"})"};
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, true));

    Http::TestResponseHeaderMapImpl continue_headers{{":status", "000"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_.encode100ContinueHeaders(continue_headers));
  }

private:
  const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder
  makeProtoConfig() {
    auto proto_config = bookstoreProtoConfig();
    return proto_config;
  }
};

class GrpcJsonTranscoderFilterConvertGrpcStatusTest
    : public GrpcJsonTranscoderFilterGrpcStatusTest {
public:
  GrpcJsonTranscoderFilterConvertGrpcStatusTest()
      : GrpcJsonTranscoderFilterGrpcStatusTest(makeProtoConfig()) {}

private:
  const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder
  makeProtoConfig() {
    auto proto_config = bookstoreProtoConfig();
    proto_config.set_convert_grpc_status(true);
    return proto_config;
  }
};

// Single headers frame with end_stream flag (trailer), no grpc-status-details-bin header.
TEST_F(GrpcJsonTranscoderFilterConvertGrpcStatusTest, TranscodingTextHeadersInTrailerOnlyResponse) {
  const std::string expected_response(R"({"code":5,"message":"Resource not found"})");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
      .WillOnce(Invoke([&expected_response](Buffer::Instance& data, bool) {
        EXPECT_EQ(expected_response, data.toString());
      }));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"},
                                                   {"grpc-status", "5"},
                                                   {"grpc-message", "Resource not found"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, true));
  EXPECT_EQ("404", response_headers.get_(":status"));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));
  EXPECT_FALSE(response_headers.has("grpc-status"));
  EXPECT_FALSE(response_headers.has("grpc-message"));
}

// Trailer-only response with grpc-status-details-bin header.
TEST_F(GrpcJsonTranscoderFilterConvertGrpcStatusTest,
       TranscodingBinaryHeaderInTrailerOnlyResponse) {
  const std::string expected_response(R"({"code":5,"message":"Resource not found"})");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
      .WillOnce(Invoke([&expected_response](Buffer::Instance& data, bool) {
        EXPECT_EQ(expected_response, data.toString());
      }));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"content-type", "application/grpc"},
      {"grpc-status", "5"},
      {"grpc-message", "unused"},
      {"grpc-status-details-bin", "CAUSElJlc291cmNlIG5vdCBmb3VuZA"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, true));
  EXPECT_EQ("404", response_headers.get_(":status"));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));
  EXPECT_FALSE(response_headers.has("grpc-status"));
  EXPECT_FALSE(response_headers.has("grpc-message"));
  EXPECT_FALSE(response_headers.has("grpc-status-details-bin"));
}

// Trailer-only response with grpc-status-details-bin header with details.
// Also tests that a user-defined type from a proto descriptor in config can be used in details.
TEST_F(GrpcJsonTranscoderFilterConvertGrpcStatusTest,
       TranscodingBinaryHeaderWithDetailsInTrailerOnlyResponse) {
  const std::string expected_response(
      "{\"code\":5,\"message\":\"Error\",\"details\":"
      "[{\"@type\":\"type.googleapis.com/helloworld.HelloReply\",\"message\":\"details\"}]}");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
      .WillOnce(Invoke([&expected_response](Buffer::Instance& data, bool) {
        EXPECT_EQ(expected_response, data.toString());
      }));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"content-type", "application/grpc"},
      {"grpc-status", "5"},
      {"grpc-message", "unused"},
      {"grpc-status-details-bin",
       "CAUSBUVycm9yGjYKKXR5cGUuZ29vZ2xlYXBpcy5jb20vaGVsbG93b3JsZC5IZWxsb1JlcGx5EgkKB2RldGFpbHM"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, true));
}

// Response with a header frame and a trailer frame.
// (E.g. a gRPC server sends metadata and then it sends an error.)
TEST_F(GrpcJsonTranscoderFilterConvertGrpcStatusTest, TranscodingStatusFromTrailer) {
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));
  std::string expected_response(R"({"code":5,"message":"Resource not found"})");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
      .WillOnce(Invoke([&expected_response](Buffer::Instance& data, bool) {
        EXPECT_EQ(expected_response, data.toString());
      }));
  Http::TestResponseTrailerMapImpl response_trailers{
      {"grpc-status", "5"},
      {"grpc-message", "unused"},
      {"grpc-status-details-bin", "CAUSElJlc291cmNlIG5vdCBmb3VuZA"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("404", response_headers.get_(":status"));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));
  EXPECT_FALSE(response_headers.has("grpc-status"));
  EXPECT_FALSE(response_headers.has("grpc-message"));
  EXPECT_FALSE(response_headers.has("grpc-status-details-bin"));
}

TEST_F(GrpcJsonTranscoderFilterGrpcStatusTest, TranscodingInvalidGrpcStatusFromTrailer) {
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "1024"},
                                                     {"grpc-message", "message"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("500", response_headers.get_(":status"));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));
  EXPECT_EQ("1024", response_headers.get_("grpc-status"));
  EXPECT_TRUE(response_headers.has("grpc-message"));
}

// Server sends a response body, don't replace it.
TEST_F(GrpcJsonTranscoderFilterConvertGrpcStatusTest, SkipTranscodingStatusIfBodyIsPresent) {
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                                   {":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("application/json", response_headers.get_("content-type"));

  bookstore::Shelf response;
  response.set_id(20);
  response.set_theme("Children");

  auto response_data = Grpc::Common::serializeToGrpcFrame(response);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_.encodeData(*response_data, false));

  const std::string response_json = response_data->toString();
  EXPECT_EQ(R"({"id":"20","theme":"Children"})", response_json);

  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _)).Times(0);

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));
}

struct GrpcJsonTranscoderFilterPrintTestParam {
  std::string config_json_;
  std::string expected_response_;
};

class GrpcJsonTranscoderFilterPrintTest
    : public testing::TestWithParam<GrpcJsonTranscoderFilterPrintTestParam>,
      public GrpcJsonTranscoderFilterTestBase {
protected:
  GrpcJsonTranscoderFilterPrintTest() {
    envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config;
    TestUtility::loadFromJson(TestEnvironment::substitute(GetParam().config_json_), proto_config);
    config_ = new JsonTranscoderConfig(proto_config, *api_);
    filter_ = new JsonTranscoderFilter(*config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  ~GrpcJsonTranscoderFilterPrintTest() override {
    delete filter_;
    delete config_;
  }

  JsonTranscoderConfig* config_;
  JsonTranscoderFilter* filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_P(GrpcJsonTranscoderFilterPrintTest, PrintOptions) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "GET"}, {":path", "/authors/101"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  bookstore::Author author;
  author.set_id(101);
  author.set_gender(bookstore::Author_Gender_MALE);
  author.set_last_name("Shakespeare");

  const auto response_data = Grpc::Common::serializeToGrpcFrame(author);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->encodeData(*response_data, false));

  std::string response_json = response_data->toString();
  EXPECT_EQ(GetParam().expected_response_, response_json);
}

INSTANTIATE_TEST_SUITE_P(
    GrpcJsonTranscoderFilterPrintOptions, GrpcJsonTranscoderFilterPrintTest,
    ::testing::Values(
        GrpcJsonTranscoderFilterPrintTestParam{
            R"({
     "proto_descriptor": "{{ test_rundir }}/test/proto/bookstore.descriptor",
     "services": ["bookstore.Bookstore"]
    })",
            R"({"id":"101","gender":"MALE","lname":"Shakespeare"})"},
        GrpcJsonTranscoderFilterPrintTestParam{R"({
     "proto_descriptor": "{{ test_rundir }}/test/proto/bookstore.descriptor",
     "services": ["bookstore.Bookstore"],
     "print_options":{"add_whitespace": true}
    })",
                                               R"({
 "id": "101",
 "gender": "MALE",
 "lname": "Shakespeare"
}
)"},
        GrpcJsonTranscoderFilterPrintTestParam{
            R"({
     "proto_descriptor": "{{ test_rundir }}/test/proto/bookstore.descriptor",
     "services": ["bookstore.Bookstore"],
     "print_options":{"always_print_primitive_fields": true}
    })",
            R"({"id":"101","gender":"MALE","firstName":"","lname":"Shakespeare"})"},
        GrpcJsonTranscoderFilterPrintTestParam{R"({
     "proto_descriptor": "{{ test_rundir }}/test/proto/bookstore.descriptor",
     "services": ["bookstore.Bookstore"],
     "print_options":{"always_print_enums_as_ints": true}
    })",
                                               R"({"id":"101","gender":1,"lname":"Shakespeare"})"},
        GrpcJsonTranscoderFilterPrintTestParam{
            R"({
     "proto_descriptor": "{{ test_rundir }}/test/proto/bookstore.descriptor",
     "services": ["bookstore.Bookstore"],
     "print_options":{"preserve_proto_field_names": true}
    })",
            R"({"id":"101","gender":"MALE","last_name":"Shakespeare"})"}));

} // namespace
} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
