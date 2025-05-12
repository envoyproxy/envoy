#include "mocks.h"

#include "gmock/gmock.h"

using testing::Return;
using testing::ReturnRef;

using testing::_;
using testing::Invoke;
using testing::ReturnRef;
using testing::SaveArg;

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
  ON_CALL(*this, addIdleCallback(_)).WillByDefault(SaveArg<0>(&idle_cb_));
}
MockInstance::~MockInstance() = default;

Envoy::ConnectionPool::MockCancellable* MockInstance::newConnectionImpl(Callbacks& cb) {
  handles_.emplace_back();
  callbacks_.push_back(&cb);
  return &handles_.back();
}

void MockInstance::poolFailure(PoolFailureReason reason, bool host_null) {
  Callbacks* cb = callbacks_.front();
  callbacks_.pop_front();
  handles_.pop_front();
  if (host_null) {
    cb->onPoolFailure(reason, "", nullptr);
  } else {
    cb->onPoolFailure(reason, "", host_);
  }
}

void MockInstance::poolReady(Network::MockClientConnection& conn) {
  Callbacks* cb = callbacks_.front();
  callbacks_.pop_front();
  handles_.pop_front();

  ON_CALL(*connection_data_, connection()).WillByDefault(ReturnRef(conn));

  connection_data_->release_callback_ = [&]() -> void { released(conn); };

  cb->onPoolReady(std::move(connection_data_), host_);

  connection_data_ = std::make_unique<NiceMock<MockConnectionData>>();
}

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
