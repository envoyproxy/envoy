#include "mocks.h"

#include <cstdint>

#include "source/common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

void PrintTo(const RespValue& value, std::ostream* os) { *os << value.toString(); }

void PrintTo(const RespValuePtr& value, std::ostream* os) { *os << value->toString(); }

MockEncoder::MockEncoder() {
  ON_CALL(*this, encode(_, _))
      .WillByDefault([this](const Common::Redis::RespValue& value, Buffer::Instance& out) -> void {
        real_encoder_.encode(value, out);
      });
  ON_CALL(*this, setProtocolVersion(_))
      .WillByDefault([this](Common::Redis::RespProtocolVersion version) -> void {
        real_encoder_.setProtocolVersion(version);
      });
}

MockEncoder::~MockEncoder() = default;

MockDecoder::MockDecoder() = default;
MockDecoder::~MockDecoder() = default;

namespace Client {

MockClient::MockClient() {
  ON_CALL(*this, addConnectionCallbacks(_))
      .WillByDefault([this](Network::ConnectionCallbacks& callbacks) -> void {
        callbacks_.push_back(&callbacks);
      });
  ON_CALL(*this, close()).WillByDefault([this]() -> void {
    raiseEvent(Network::ConnectionEvent::LocalClose);
  });
}

MockClient::~MockClient() = default;

MockPoolRequest::MockPoolRequest() = default;
MockPoolRequest::~MockPoolRequest() = default;

MockClientCallbacks::MockClientCallbacks() = default;
MockClientCallbacks::~MockClientCallbacks() = default;

} // namespace Client

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
