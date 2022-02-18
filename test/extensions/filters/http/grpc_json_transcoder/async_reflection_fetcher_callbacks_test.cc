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

class AsyncReflectionFetcherCallbacksTest : public testing::Test {
public:
  AsyncReflectionFetcherCallbacksTest() {}
};

/**
 * Verify that we registered with the init manager when requestFileDescriptors
 * is invoked.
 */
TEST_F(AsyncReflectionFetcherCallbacksTest, OnReceiveMessage) {
  MockFunction<void(AsyncReflectionFetcherCallbacks*,
                    std::unique_ptr<grpc::reflection::v1alpha::ServerReflectionResponse> &&)>
      mock_on_success_callback;
  AsyncReflectionFetcherCallbacks async_reflection_fetcher_callbacks(
      mock_on_success_callback.AsStdFunction());
  std::unique_ptr<ServerReflectionResponse> message = std::make_unique<ServerReflectionResponse>();

  EXPECT_CALL(mock_on_success_callback, Call(_, _));
  async_reflection_fetcher_callbacks.onReceiveMessage(std::move(message));
}

TEST_F(AsyncReflectionFetcherCallbacksTest, OnRemoteClose) {
  MockFunction<void(AsyncReflectionFetcherCallbacks*,
                    std::unique_ptr<grpc::reflection::v1alpha::ServerReflectionResponse> &&)>
      mock_on_success_callback;
  AsyncReflectionFetcherCallbacks async_reflection_fetcher_callbacks(
      mock_on_success_callback.AsStdFunction());

  EXPECT_CALL(mock_on_success_callback, Call(_, _)).Times(0);
  async_reflection_fetcher_callbacks.onRemoteClose(Envoy::Grpc::Status::Unknown,
                                                   "An unknown error occurred");
}

} // namespace
} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
