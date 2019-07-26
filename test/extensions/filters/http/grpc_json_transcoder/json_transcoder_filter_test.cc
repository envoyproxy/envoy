#include <fstream>
#include <functional>

#include "common/buffer/buffer_impl.h"
#include "common/config/filter_json.h"
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
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

using Envoy::Protobuf::MethodDescriptor;

using Envoy::Protobuf::FileDescriptorProto;
using Envoy::Protobuf::FileDescriptorSet;
using Envoy::Protobuf::util::MessageDifferencer;
using Envoy::ProtobufUtil::Status;
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

  Api::ApiPtr api_;
};

class GrpcJsonTranscoderConfigTest : public testing::Test, public GrpcJsonTranscoderFilterTestBase {
protected:
  const envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder
  getProtoConfig(const std::string& descriptor_path, const std::string& service_name,
                 bool match_incoming_request_route = false,
                 const std::vector<std::string>& ignored_query_parameters = {}) {
    std::string json_string = "{\"proto_descriptor\": \"" + descriptor_path +
                              "\",\"services\": [\"" + service_name + "\"]}";
    auto json_config = Json::Factory::loadFromString(json_string);
    envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder proto_config;
    Envoy::Config::FilterJson::translateGrpcJsonTranscoder(*json_config, proto_config);
    proto_config.set_match_incoming_request_route(match_incoming_request_route);
    for (const std::string& query_param : ignored_query_parameters) {
      proto_config.add_ignored_query_parameters(query_param);
    }

    return proto_config;
  }

  std::string makeProtoDescriptor(std::function<void(FileDescriptorSet&)> process) {
    FileDescriptorSet descriptor_set;
    descriptor_set.ParseFromString(api_->fileSystem().fileReadToEnd(
        TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));

    process(descriptor_set);

    TestUtility::createDirectory(TestEnvironment::temporaryPath("envoy_test"));
    std::string path = TestEnvironment::temporaryPath("envoy_test/proto.descriptor");
    std::ofstream file(path);
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
  envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder proto_config;
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
  envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder proto_config;
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

  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/shelves"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  const MethodDescriptor* method_descriptor;
  auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_descriptor);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder);
  EXPECT_EQ("bookstore.Bookstore.ListShelves", method_descriptor->full_name());
}

TEST_F(GrpcJsonTranscoderConfigTest, CreateTranscoderAutoMap) {
  auto proto_config = getProtoConfig(
      TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"), "bookstore.Bookstore");
  proto_config.set_auto_mapping(true);

  JsonTranscoderConfig config(proto_config, *api_);

  Http::TestHeaderMapImpl headers{{":method", "POST"},
                                  {":path", "/bookstore.Bookstore/DeleteShelf"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  const MethodDescriptor* method_descriptor;
  auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_descriptor);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder);
  EXPECT_EQ("bookstore.Bookstore.DeleteShelf", method_descriptor->full_name());
}

TEST_F(GrpcJsonTranscoderConfigTest, InvalidQueryParameter) {
  JsonTranscoderConfig config(
      getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"),
                     "bookstore.Bookstore"),
      *api_);

  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/shelves?foo=bar"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  const MethodDescriptor* method_descriptor;
  auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_descriptor);

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

  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/shelves?foo=bar"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  const MethodDescriptor* method_descriptor;
  auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_descriptor);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder);
}

TEST_F(GrpcJsonTranscoderConfigTest, IgnoredQueryParameter) {
  std::vector<std::string> ignored_query_parameters = {"key"};
  JsonTranscoderConfig config(
      getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"),
                     "bookstore.Bookstore", false, ignored_query_parameters),
      *api_);

  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/shelves?key=API_KEY"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  const MethodDescriptor* method_descriptor;
  auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_descriptor);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder);
  EXPECT_EQ("bookstore.Bookstore.ListShelves", method_descriptor->full_name());
}

TEST_F(GrpcJsonTranscoderConfigTest, InvalidVariableBinding) {
  HttpRule http_rule;
  http_rule.set_get("/book/{b}");
  JsonTranscoderConfig config(getProtoConfig(makeProtoDescriptor([&](FileDescriptorSet& pb) {
                                               setGetBookHttpRule(pb, http_rule);
                                             }),
                                             "bookstore.Bookstore"),
                              *api_);

  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/book/1"}};

  TranscoderInputStreamImpl request_in, response_in;
  std::unique_ptr<Transcoder> transcoder;
  const MethodDescriptor* method_descriptor;
  auto status =
      config.createTranscoder(headers, request_in, response_in, transcoder, method_descriptor);

  EXPECT_EQ(Code::INVALID_ARGUMENT, status.error_code());
  EXPECT_EQ("Could not find field \"b\" in the type \"bookstore.GetBookRequest\".",
            status.error_message());
  EXPECT_FALSE(transcoder);
}

class GrpcJsonTranscoderFilterTest : public testing::Test, public GrpcJsonTranscoderFilterTestBase {
protected:
  GrpcJsonTranscoderFilterTest(const bool match_incoming_request_route = false)
      : config_(bookstoreProtoConfig(match_incoming_request_route), *api_), filter_(config_) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  const envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder
  bookstoreProtoConfig(const bool match_incoming_request_route) {
    std::string json_string = "{\"proto_descriptor\": \"" + bookstoreDescriptorPath() +
                              "\",\"services\": [\"bookstore.Bookstore\"]}";
    auto json_config = Json::Factory::loadFromString(json_string);
    envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder proto_config{};
    Envoy::Config::FilterJson::translateGrpcJsonTranscoder(*json_config, proto_config);
    proto_config.set_match_incoming_request_route(match_incoming_request_route);
    return proto_config;
  }

  const std::string bookstoreDescriptorPath() {
    return TestEnvironment::runfilesPath("test/proto/bookstore.descriptor");
  }

  // TODO(lizan): Add a mock of JsonTranscoderConfig and test more error cases.
  JsonTranscoderConfig config_;
  JsonTranscoderFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(GrpcJsonTranscoderFilterTest, NoTranscoding) {
  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
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

  Http::TestHeaderMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Http::TestHeaderMapImpl response_headers{{"content-type", "application/grpc"},
                                           {":status", "200"}};

  Http::TestHeaderMapImpl expected_response_headers{{"content-type", "application/grpc"},
                                                    {":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(expected_response_headers, response_headers);

  Buffer::OwnedImpl response_data{"{}"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_data, false));
  EXPECT_EQ(2, response_data.length());

  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}};
  Http::TestHeaderMapImpl expected_response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(expected_response_trailers, response_trailers);
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryPost) {
  Http::TestHeaderMapImpl request_headers{
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

  Http::TestHeaderMapImpl continue_headers{{":status", "000"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.encodeMetadata(metadata_map));

  Http::TestHeaderMapImpl response_headers{{"content-type", "application/grpc"},
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

  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}, {"grpc-message", ""}};

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(response_trailers));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryPostWithPackageServiceMethodPath) {
  Http::TestHeaderMapImpl request_headers{
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

  Http::TestHeaderMapImpl continue_headers{{":status", "000"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::TestHeaderMapImpl response_headers{{"content-type", "application/grpc"},
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

  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}, {"grpc-message", ""}};

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(response_trailers));
}

TEST_F(GrpcJsonTranscoderFilterTest, ForwardUnaryPostGrpc) {
  Http::TestHeaderMapImpl request_headers{
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

  Http::TestHeaderMapImpl continue_headers{{":status", "000"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::TestHeaderMapImpl response_headers{{"content-type", "application/grpc"},
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

  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}, {"grpc-message", ""}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(response_trailers));
}

class GrpcJsonTranscoderFilterSkipRecalculatingTest : public GrpcJsonTranscoderFilterTest {
public:
  GrpcJsonTranscoderFilterSkipRecalculatingTest() : GrpcJsonTranscoderFilterTest(true) {}
};

TEST_F(GrpcJsonTranscoderFilterSkipRecalculatingTest, TranscodingUnaryPostSkipRecalculate) {
  Http::TestHeaderMapImpl request_headers{
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
  Http::TestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelf", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\""};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([](Http::HeaderMap& headers, bool end_stream) {
        EXPECT_EQ("400", headers.Status()->value().getStringView());
        EXPECT_FALSE(end_stream);
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(request_data, true));
  EXPECT_EQ(0, request_data.length());
  EXPECT_EQ(decoder_callbacks_.details_, "grpc_json_transcode_failure{INVALID_ARGUMENT}");
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryTimeout) {
  Http::TestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelf", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\"}"};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, true));

  Http::TestHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(request_data, true));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryNotGrpcResponse) {
  Http::TestHeaderMapImpl request_headers{
      {"content-type", "application/json"}, {":method", "POST"}, {":path", "/shelf"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/bookstore.Bookstore/CreateShelf", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Buffer::OwnedImpl request_data{"{\"theme\": \"Children\"}"};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_data, true));

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(request_data, true));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryWithHttpBodyAsOutput) {
  Http::TestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/index"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/index", request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/GetIndex", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Http::TestHeaderMapImpl response_headers{{"content-type", "application/grpc"},
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

  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}, {"grpc-message", ""}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(response_trailers));
}

TEST_F(GrpcJsonTranscoderFilterTest, TranscodingUnaryWithHttpBodyAsOutputAndSplitTwoEncodeData) {
  Http::TestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/index"}};

  EXPECT_CALL(decoder_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ("application/grpc", request_headers.get_("content-type"));
  EXPECT_EQ("/index", request_headers.get_("x-envoy-original-path"));
  EXPECT_EQ("/bookstore.Bookstore/GetIndex", request_headers.get_(":path"));
  EXPECT_EQ("trailers", request_headers.get_("te"));

  Http::TestHeaderMapImpl response_headers{{"content-type", "application/grpc"},
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

  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}, {"grpc-message", ""}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(response_trailers));
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
    auto json_config =
        Json::Factory::loadFromString(TestEnvironment::substitute(GetParam().config_json_));
    envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder proto_config{};
    Envoy::Config::FilterJson::translateGrpcJsonTranscoder(*json_config, proto_config);
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
  Http::TestHeaderMapImpl request_headers{
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
