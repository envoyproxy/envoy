#pragma once

#include <string>
#include <unordered_map>

#include "envoy/buffer/buffer.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

enum class ProtocolType : uint8_t {
  Dubbo = 0,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST PROTOCOL TYPE
  LastProtocolType = Dubbo,
};

/**
 * Names of available Protocol implementations.
 */
class ProtocolNameValues {
public:
  struct ProtocolTypeHash {
    template <typename T> std::size_t operator()(T t) const { return static_cast<std::size_t>(t); }
  };

  typedef std::unordered_map<ProtocolType, std::string, ProtocolTypeHash> ProtocolTypeNameMap;

  const ProtocolTypeNameMap protocolTypeNameMap = {
      {ProtocolType::Dubbo, "dubbo"},
  };

  const std::string& fromType(ProtocolType type) const {
    const auto& itor = protocolTypeNameMap.find(type);
    if (itor != protocolTypeNameMap.end()) {
      return itor->second;
    }

    NOT_REACHED_GCOVR_EXCL_LINE;
  }
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

  /**
   * @return ProtocolType the protocol type
   */
  virtual ProtocolType type() const PURE;

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

/**
 * Implemented by each Dubbo protocol and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedProtocolConfigFactory {
public:
  virtual ~NamedProtocolConfigFactory() {}

  /**
   * Create a particular Dubbo protocol.
   * @param callbacks the callbacks to be notified of protocol decodes.
   * @return ptotocol instance pointer.
   */
  virtual ProtocolPtr createProtocol(ProtocolCallbacks& callbacks) PURE;

  /**
   * @return std::string the identifying name for a particular implementation of Dubbo protocol
   * produced by the factory.
   */
  virtual std::string name() PURE;

  /**
   * Convenience method to lookup a factory by type.
   * @param ProtocolType the protocol type.
   * @return NamedProtocolConfigFactory& for the ProtocolType.
   */
  static NamedProtocolConfigFactory& getFactory(ProtocolType type) {
    const std::string& name = ProtocolNames::get().fromType(type);
    return Envoy::Config::Utility::getAndCheckFactory<NamedProtocolConfigFactory>(name);
  }
};

/**
 * ProtocolFactoryBase provides a template for a trivial NamedProtocolConfigFactory.
 */
template <class ProtocolImpl> class ProtocolFactoryBase : public NamedProtocolConfigFactory {
  ProtocolPtr createProtocol(ProtocolCallbacks& callbacks) override {
    return std::move(std::make_unique<ProtocolImpl>(callbacks));
  }

  std::string name() override { return name_; }

protected:
  ProtocolFactoryBase(ProtocolType type) : name_(ProtocolNames::get().fromType(type)) {}

private:
  const std::string name_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
