#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

// Supported serialization type
enum class SerializationType : uint8_t {
  Hessian = 2,
  Json = 6,
};

// Message Type
enum class MessageType : uint8_t {
  Response = 0,
  Request = 1,
  Oneway = 2,
  Exception = 3,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST MESSAGE TYPE
  LastMessageType = Exception,
};

/**
 * Dubbo protocol response status types.
 * See org.apache.dubbo.remoting.exchange
 */
enum class ResponseStatus : uint8_t {
  Ok = 20,
  ClientTimeout = 30,
  ServerTimeout = 31,
  BadRequest = 40,
  BadResponse = 50,
  ServiceNotFound = 60,
  ServiceError = 70,
  ServerError = 80,
  ClientError = 90,
  ServerThreadpoolExhaustedError = 100,
};

enum class RpcResponseType : uint8_t {
  ResponseWithException = 0,
  ResponseWithValue = 1,
  ResponseWithNullValue = 2,
  ResponseWithExceptionWithAttachments = 3,
  ResponseValueWithAttachments = 4,
  ResponseNullValueWithAttachments = 5,
};

class Message {
public:
  virtual ~Message() = default;
  virtual MessageType messageType() const PURE;
  virtual int32_t bodySize() const PURE;
  virtual bool isEvent() const PURE;
  virtual int64_t requestId() const PURE;
  virtual std::string toString() const PURE;
};

class RequestMessage : public virtual Message {
public:
  ~RequestMessage() override = default;
  virtual SerializationType serializationType() const PURE;
  virtual bool isTwoWay() const PURE;
};

using RequestMessagePtr = std::unique_ptr<RequestMessage>;

class ResponseMessage : public virtual Message {
public:
  ~ResponseMessage() override = default;
  virtual ResponseStatus responseStatus() const PURE;
};

using ResponseMessagePtr = std::unique_ptr<ResponseMessage>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
