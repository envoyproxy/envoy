#include "mocks.h"

#include "test/mocks/http/mocks.h"

namespace Envoy {
namespace Grpc {

MockAsyncClient::MockAsyncClient() {
  async_request_ = std::make_unique<testing::NiceMock<Grpc::MockAsyncRequest>>();
  ON_CALL(*this, sendRaw(_, _, _, _, _, _)).WillByDefault(Return(async_request_.get()));
}
MockAsyncClient::~MockAsyncClient() = default;

MockAsyncRequest::MockAsyncRequest() = default;
MockAsyncRequest::~MockAsyncRequest() = default;

MockAsyncStream::MockAsyncStream() = default;
MockAsyncStream::~MockAsyncStream() = default;

MockAsyncClientFactory::MockAsyncClientFactory() {
  ON_CALL(*this, create()).WillByDefault(Invoke([] {
    return std::make_unique<testing::NiceMock<Grpc::MockAsyncClient>>();
  }));
}
MockAsyncClientFactory::~MockAsyncClientFactory() = default;

MockAsyncClientManager::MockAsyncClientManager() {
  ON_CALL(*this, factoryForGrpcService(_, _, _))
      .WillByDefault(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<testing::NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
}

MockAsyncClientManager::~MockAsyncClientManager() = default;

} // namespace Grpc
} // namespace Envoy
