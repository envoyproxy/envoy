#include "mocks.h"

#include "gmock/gmock.h"

using testing::ReturnRef;

using testing::_;
using testing::Invoke;
using testing::ReturnRef;

namespace Envoy {
namespace Tcp {
namespace ConnectionPool {

MockUpstreamCallbacks::MockUpstreamCallbacks() = default;
MockUpstreamCallbacks::~MockUpstreamCallbacks() = default;

MockConnectionData::MockConnectionData() = default;
MockConnectionData::~MockConnectionData() {
  if (release_callback_) {
    release_callback_();
  }
}

MockInstance::MockInstance() {
  ON_CALL(*this, newConnection(_)).WillByDefault(Invoke([&](Callbacks& cb) -> Cancellable* {
    return newConnectionImpl(cb);
  }));
  ON_CALL(*this, host()).WillByDefault(Return(host_));
}
MockInstance::~MockInstance() = default;

Envoy::ConnectionPool::MockCancellable* MockInstance::newConnectionImpl(Callbacks& cb) {
  handles_.emplace_back();
  callbacks_.push_back(&cb);
  return &handles_.back();
}

void MockInstance::poolFailure(PoolFailureReason reason) {
  Callbacks* cb = callbacks_.front();
  callbacks_.pop_front();
  handles_.pop_front();

  cb->onPoolFailure(reason, host_);
}

void MockInstance::poolReady(Network::MockClientConnection& conn) {
  Callbacks* cb = callbacks_.front();
  callbacks_.pop_front();
  handles_.pop_front();

  ON_CALL(*connection_data_, connection()).WillByDefault(ReturnRef(conn));

  connection_data_->release_callback_ = [&]() -> void { released(conn); };

  cb->onPoolReady(std::move(connection_data_), host_);
}

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
