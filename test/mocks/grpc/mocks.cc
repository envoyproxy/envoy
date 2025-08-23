#include "mocks.h"

#include "test/mocks/http/mocks.h"

using testing::Return;

namespace Envoy {
namespace Grpc {

MockAsyncClient::MockAsyncClient() {
  async_request_ = std::make_unique<testing::NiceMock<Grpc::MockAsyncRequest>>();
  ON_CALL(*this, sendRaw(_, _, _, _, _, _))
      .WillByDefault(Invoke([this](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                                   RawAsyncRequestCallbacks&, Tracing::Span&,
                                   const Http::AsyncClient::RequestOptions&) {
        send_count_++;
        return async_request_.get();
      }));

  // Because this method is used in debug logs, coverage and non-coverage builds have different
  // expectations, so add this to prevent failures due to "uninteresting mock function call".
  EXPECT_CALL(*this, destination())
      .Times(testing::AnyNumber())
      .WillRepeatedly(Return(absl::string_view("unspecified_mock_destination")));
}
MockAsyncClient::~MockAsyncClient() = default;

MockAsyncRequest::MockAsyncRequest() = default;
MockAsyncRequest::~MockAsyncRequest() = default;

MockAsyncStream::MockAsyncStream() = default;
MockAsyncStream::~MockAsyncStream() = default;

MockAsyncClientFactory::MockAsyncClientFactory() {
  ON_CALL(*this, createUncachedRawAsyncClient()).WillByDefault(Invoke([] {
    return std::make_unique<testing::NiceMock<Grpc::MockAsyncClient>>();
  }));
}
MockAsyncClientFactory::~MockAsyncClientFactory() = default;

MockAsyncClientManager::MockAsyncClientManager() {
  ON_CALL(*this, getOrCreateRawAsyncClient(_, _, _)).WillByDefault(Return(nullptr));
  ON_CALL(*this, factoryForGrpcService(_, _, _))
      .WillByDefault(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<testing::NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
}

MockAsyncClientManager::~MockAsyncClientManager() = default;

} // namespace Grpc
} // namespace Envoy
