#include <fstream>
#include <functional>
#include <memory>

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;

using Envoy::Protobuf::FileDescriptorProto;
using Envoy::Protobuf::FileDescriptorSet;
using Envoy::Protobuf::util::MessageDifferencer;
using Envoy::ProtobufUtil::StatusCode;
using google::api::HttpRule;
using google::grpc::transcoding::Transcoder;
using TranscoderPtr = std::unique_ptr<Transcoder>;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {
namespace {

class DescriptorPoolBuilderTest : public testing::Test {
public:
  DescriptorPoolBuilderTest() : api_(Api::createApiForTest()) {}

protected:
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

  Api::ApiPtr api_;
};

TEST_F(DescriptorPoolBuilderTest, ParseBinaryConfig) {
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config;
  proto_config.set_proto_descriptor_bin(api_->fileSystem().fileReadToEnd(
      TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  proto_config.add_services("bookstore.Bookstore");
  DescriptorPoolBuilder builder(proto_config, *api_);
  MockFunction<void(std::unique_ptr<Protobuf::DescriptorPool> &&)> mock_descriptor_pool_available;
  EXPECT_CALL(mock_descriptor_pool_available, Call(_));
  EXPECT_NO_THROW(builder.requestDescriptorPool(mock_descriptor_pool_available.AsStdFunction()));
}

TEST_F(DescriptorPoolBuilderTest, IncompleteProto) {
  auto proto_config = getProtoConfig(makeProtoDescriptor([&](FileDescriptorSet& pb) {
                                       stripImports(pb, "test/proto/bookstore.proto");
                                     }),
                                     "bookstore.Bookstore");
  DescriptorPoolBuilder builder(proto_config, *api_);
  MockFunction<void(std::unique_ptr<Protobuf::DescriptorPool> &&)> mock_descriptor_pool_available;
  EXPECT_CALL(mock_descriptor_pool_available, Call(_)).Times(0);
  EXPECT_LOG_CONTAINS(
      "error", "transcoding_filter: Missing dependencies when building proto descriptor pool",
      builder.requestDescriptorPool(mock_descriptor_pool_available.AsStdFunction()));
}

TEST_F(DescriptorPoolBuilderTest, NonProto) {
  auto proto_config = getProtoConfig(TestEnvironment::runfilesPath("test/proto/bookstore.proto"),
                                     "grpc.service.UnknownService");
  DescriptorPoolBuilder builder(proto_config, *api_);
  MockFunction<void(std::unique_ptr<Protobuf::DescriptorPool> &&)> mock_descriptor_pool_available;
  EXPECT_CALL(mock_descriptor_pool_available, Call(_)).Times(0);
  EXPECT_THROW_WITH_MESSAGE(
      builder.requestDescriptorPool(mock_descriptor_pool_available.AsStdFunction()), EnvoyException,
      "transcoding_filter: Unable to parse proto descriptor");
}

TEST_F(DescriptorPoolBuilderTest, NonBinaryProto) {
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config;
  proto_config.set_proto_descriptor_bin("This is invalid proto");
  proto_config.add_services("bookstore.Bookstore");
  MockFunction<void(std::unique_ptr<Protobuf::DescriptorPool> &&)> mock_descriptor_pool_available;
  DescriptorPoolBuilder builder(proto_config, *api_);
  EXPECT_THROW_WITH_MESSAGE(
      builder.requestDescriptorPool(mock_descriptor_pool_available.AsStdFunction()), EnvoyException,
      "transcoding_filter: Unable to parse proto descriptor");
}

} // namespace
} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
