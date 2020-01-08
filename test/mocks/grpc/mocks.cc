#include "mocks.h"

namespace Envoy {
namespace Grpc {

MockAsyncRequest::MockAsyncRequest() = default;
MockAsyncRequest::~MockAsyncRequest() = default;

MockAsyncStream::MockAsyncStream() = default;
MockAsyncStream::~MockAsyncStream() = default;

MockAsyncClientFactory::MockAsyncClientFactory() = default;
MockAsyncClientFactory::~MockAsyncClientFactory() = default;

MockAsyncClientManager::MockAsyncClientManager() {
  ON_CALL(*this, factoryForGrpcService(_, _, _))
      .WillByDefault(
          Invoke([](const envoy::config::core::v3alpha::GrpcService&, Stats::Scope&, bool) {
            return std::make_unique<testing::NiceMock<Grpc::MockAsyncClientFactory>>();
          }));
}

MockAsyncClientManager::~MockAsyncClientManager() = default;

} // namespace Grpc
} // namespace Envoy
