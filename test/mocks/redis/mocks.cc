#include "mocks.h"

#include "common/common/assert.h"

using testing::_;
using testing::Invoke;

namespace Redis {

bool operator==(const RespValue& lhs, const RespValue& rhs) {
  if (lhs.type() != rhs.type()) {
    return false;
  }

  switch (lhs.type()) {
  case RespType::Array: {
    if (lhs.asArray().size() != rhs.asArray().size()) {
      return false;
    }

    bool equal = true;
    for (uint64_t i = 0; i < lhs.asArray().size(); i++) {
      equal &= (lhs.asArray()[i] == rhs.asArray()[i]);
    }

    return equal;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    return lhs.asString() == rhs.asString();
  }
  case RespType::Null: {
    return true;
  }
  case RespType::Integer: {
    return lhs.asInteger() == rhs.asInteger();
  }
  }

  NOT_IMPLEMENTED;
}

MockEncoder::MockEncoder() {}
MockEncoder::~MockEncoder() {}

MockDecoder::MockDecoder() {}
MockDecoder::~MockDecoder() {}

namespace ConnPool {

MockClient::MockClient() {
  ON_CALL(*this, addConnectionCallbacks(_))
      .WillByDefault(Invoke([this](Network::ConnectionCallbacks& callbacks)
                                -> void { callbacks_.push_back(&callbacks); }));
  ON_CALL(*this, close())
      .WillByDefault(
          Invoke([this]() -> void { raiseEvents(Network::ConnectionEvent::LocalClose); }));
}

MockClient::~MockClient() {}

MockActiveRequest::MockActiveRequest() {}
MockActiveRequest::~MockActiveRequest() {}

MockActiveRequestCallbacks::MockActiveRequestCallbacks() {}
MockActiveRequestCallbacks::~MockActiveRequestCallbacks() {}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // ConnPool
} // Redis
