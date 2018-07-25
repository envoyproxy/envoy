#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Tcp {
namespace ConnectionPool {

MockCancellable::MockCancellable() {}
MockCancellable::~MockCancellable() {}

MockUpstreamCallbacks::MockUpstreamCallbacks() {}
MockUpstreamCallbacks::~MockUpstreamCallbacks() {}

MockConnectionData::MockConnectionData() {}
MockConnectionData::~MockConnectionData() {}

MockInstance::MockInstance() {
  ON_CALL(*this, newConnection(_)).WillByDefault(Invoke([&](Callbacks& cb) -> Cancellable* {
    return newConnectionImpl(cb);
  }));
}
MockInstance::~MockInstance() {}

MockCancellable* MockInstance::newConnectionImpl(Callbacks& cb) {
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

void MockInstance::poolReady() {
  Callbacks* cb = callbacks_.front();
  callbacks_.pop_front();
  handles_.pop_front();

  cb->onPoolReady(std::move(connection_data_), host_);
}

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
