#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/fmt.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * Names of available Protocol implementations.
 */
class ProtocolNameValues {
public:
  // Dubbo protocol
  const std::string Dubbo = "dubbo";
};

typedef ConstSingleton<ProtocolNameValues> ProtocolNames;

// Supported serialization type
enum class SerializationType : uint8_t {
  Hessian = 2,
  Json = 6,
};

// Message Type
enum class MessageType : uint8_t {
  Response = 0,
  Request = 1,
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

class Message {
public:
  virtual ~Message() {}
  virtual MessageType messageType() const PURE;
  virtual int32_t bodySize() const PURE;
  virtual bool isEvent() const PURE;
  virtual int64_t requestId() const PURE;
  virtual std::string toString() const PURE;
};

class RequestMessage : public virtual Message {
public:
  virtual ~RequestMessage() {}
  virtual SerializationType serializationType() const PURE;
  virtual bool isTwoWay() const PURE;
};

typedef std::unique_ptr<RequestMessage> RequestMessagePtr;

class ResponseMessage : public virtual Message {
public:
  virtual ~ResponseMessage() {}
  virtual ResponseStatus responseStatus() const PURE;
};

typedef std::unique_ptr<ResponseMessage> ResponseMessagePtr;

/**
 * ProtocolCallbacks are Dubbo protocol-level callbacks.
 */
class ProtocolCallbacks {
public:
  virtual ~ProtocolCallbacks() {}
  virtual void onRequestMessage(RequestMessagePtr&& req) PURE;
  virtual void onResponseMessage(ResponseMessagePtr&& res) PURE;
};

/**
 * See https://dubbo.incubator.apache.org/en-us/docs/dev/implementation.html
 */
class Protocol {
public:
  struct Context {
    bool is_request_ = false;
    size_t body_size_ = 0;
  };
  virtual ~Protocol() {}
  Protocol() {}
  virtual const std::string& name() const PURE;
  /*
   * decodes the dubbo protocol message, potentially invoking callbacks.
   * If successful, the message is removed from the buffer.
   *
   * @param buffer the currently buffered dubbo data.
   * @param context save the meta data of current messages
   * @return bool true if a complete message was successfully consumed, false if more data
   *                 is required.
   * @throws EnvoyException if the data is not valid for this protocol.
   */
  virtual bool decode(Buffer::Instance& buffer, Context* context) PURE;
};

typedef std::unique_ptr<Protocol> ProtocolPtr;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
