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

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Invoke;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;

using Envoy::Protobuf::FileDescriptorProto;
using Envoy::Protobuf::FileDescriptorSet;
using Envoy::Protobuf::util::MessageDifferencer;
using Envoy::ProtobufUtil::StatusCode;
using Envoy::Server::Configuration::MockFactoryContext;
using google::api::HttpRule;
using google::grpc::transcoding::Transcoder;
using TranscoderPtr = std::unique_ptr<Transcoder>;

using grpc::reflection::v1alpha::ServerReflectionResponse;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {
namespace {

class MockAsyncReflectionFetcherCallbacks : public AsyncReflectionFetcherCallbacks {
public:
  MockAsyncReflectionFetcherCallbacks()
      : AsyncReflectionFetcherCallbacks(
            [](AsyncReflectionFetcherCallbacks*, std::unique_ptr<ServerReflectionResponse>&&) {}) {}
};

class AsyncReflectionFetcherTest : public testing::Test {
public:
  AsyncReflectionFetcherTest()
      : api_(Api::createApiForTest()),
        mock_async_client_(std::make_shared<Envoy::Grpc::MockAsyncClient>()), mock_async_stream_() {
  }

protected:
  Api::ApiPtr api_;
  std::shared_ptr<Envoy::Grpc::MockAsyncClient> mock_async_client_;
  Envoy::Grpc::MockAsyncStream mock_async_stream_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

/**
 * Verify that we registered with the init manager when requestFileDescriptors
 * is invoked.
 */
TEST_F(AsyncReflectionFetcherTest, RegisterInitManager) {
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder_ReflectionConfig
      reflection_cluster_config;
  Envoy::Protobuf::RepeatedPtrField<std::string> services;
  std::vector<Grpc::RawAsyncClientSharedPtr> async_clients;
  AsyncReflectionFetcher async_reflection_fetcher(reflection_cluster_config, services,
                                                  async_clients, context_.initManager());

  MockFunction<void(Envoy::Protobuf::FileDescriptorSet)> mock_file_descriptors_available;
  EXPECT_CALL(mock_file_descriptors_available, Call(_)).Times(0);
  EXPECT_CALL(context_.init_manager_, add(_));
  async_reflection_fetcher.requestFileDescriptors(mock_file_descriptors_available.AsStdFunction());
}

/**
 * Verify that callbacks are added by startReflectionRpcs.
 */
TEST_F(AsyncReflectionFetcherTest, StartReflectionRpcs) {
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder_ReflectionConfig
      reflection_cluster_config;
  Envoy::Protobuf::RepeatedPtrField<std::string> services;
  services.Add("helloworld");
  std::vector<Grpc::RawAsyncClientSharedPtr> async_clients;
  async_clients.push_back(mock_async_client_);
  AsyncReflectionFetcher async_reflection_fetcher(reflection_cluster_config, services,
                                                  async_clients, context_.initManager());
  EXPECT_CALL(*mock_async_client_, startRaw(_, _, _, _)).WillOnce(Return(&mock_async_stream_));
  EXPECT_CALL(mock_async_stream_, sendMessageRaw_(_, true));
  EXPECT_TRUE(async_reflection_fetcher.getRemainingCallBacksForTest().size() == 0);
  async_reflection_fetcher.startReflectionRpcs();
  EXPECT_TRUE(async_reflection_fetcher.getRemainingCallBacksForTest().size() == 1);
  async_clients.clear();
}

/**
 * Verify that successful Rpc responses are handled correctly.
 */
TEST_F(AsyncReflectionFetcherTest, OnRpcCompleted) {
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder_ReflectionConfig
      reflection_cluster_config;
  Envoy::Protobuf::RepeatedPtrField<std::string> services;
  std::vector<Grpc::RawAsyncClientSharedPtr> async_clients;
  async_clients.push_back(mock_async_client_);
  AsyncReflectionFetcher async_reflection_fetcher(reflection_cluster_config, services,
                                                  async_clients, context_.initManager());

  absl::flat_hash_set<AsyncReflectionFetcherCallbacks*>& callbacks =
      async_reflection_fetcher.getRemainingCallBacksForTest();
  MockAsyncReflectionFetcherCallbacks async_reflection_fetcher_callbacks;
  callbacks.insert(&async_reflection_fetcher_callbacks);

  MockFunction<void(Envoy::Protobuf::FileDescriptorSet)> mock_file_descriptors_available;
  EXPECT_CALL(mock_file_descriptors_available, Call(_));
  // This test is not primarily testing this function, but the call is
  // necessary because  onRpcCompleted calls the function passed
  // into requestFileDescriptors.
  async_reflection_fetcher.requestFileDescriptors(mock_file_descriptors_available.AsStdFunction());

  ServerReflectionResponsePtr message = std::make_unique<ServerReflectionResponse>();
  EXPECT_FALSE(callbacks.empty());
  async_reflection_fetcher.onRpcCompleted(&async_reflection_fetcher_callbacks, std::move(message));
  EXPECT_TRUE(callbacks.empty());
}

} // namespace
} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
