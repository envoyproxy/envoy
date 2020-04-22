#pragma once

#include <string>
#include <unordered_map>

#include "envoy/buffer/buffer.h"

#include "common/common/assert.h"
#include "common/config/utility.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/dubbo_proxy/message.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * Names of available deserializer implementations.
 */
class DeserializerNameValues {
public:
  struct SerializationTypeHash {
    template <typename T> std::size_t operator()(T t) const { return static_cast<std::size_t>(t); }
  };

  using DeserializerTypeNameMap =
      std::unordered_map<SerializationType, std::string, SerializationTypeHash>;

  const DeserializerTypeNameMap deserializerTypeNameMap = {
      {SerializationType::Hessian, "hessian"},
  };

  const std::string& fromType(SerializationType type) const {
    const auto& itor = deserializerTypeNameMap.find(type);
    if (itor != deserializerTypeNameMap.end()) {
      return itor->second;
    }

    NOT_REACHED_GCOVR_EXCL_LINE;
  }
};

using DeserializerNames = ConstSingleton<DeserializerNameValues>;

/**
 * RpcInvocation represent an rpc call
 * See
 * https://github.com/apache/incubator-dubbo/blob/master/dubbo-rpc/dubbo-rpc-api/src/main/java/org/apache/dubbo/rpc/RpcInvocation.java
 */
class RpcInvocation {
public:
  virtual ~RpcInvocation() = default;
  virtual const std::string& getMethodName() const PURE;
  virtual const std::string& getServiceName() const PURE;
  virtual const std::string& getServiceVersion() const PURE;
};

using RpcInvocationPtr = std::unique_ptr<RpcInvocation>;

/**
 * RpcResult represent the result of an rpc call
 * See
 * https://github.com/apache/incubator-dubbo/blob/master/dubbo-rpc/dubbo-rpc-api/src/main/java/org/apache/dubbo/rpc/RpcResult.java
 */
class RpcResult {
public:
  virtual ~RpcResult() = default;
  virtual bool hasException() const PURE;
};

using RpcResultPtr = std::unique_ptr<RpcResult>;

class Deserializer {
public:
  virtual ~Deserializer() = default;
  /**
   * Return this Deserializer's name
   *
   * @return std::string containing the serialization name.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return SerializationType the deserializer type
   */
  virtual SerializationType type() const PURE;

  /**
   * deserialize an rpc call
   * If successful, the RpcInvocation removed from the buffer
   *
   * @param buffer the currently buffered dubbo data
   * @body_size the complete RpcInvocation size
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual void deserializeRpcInvocation(Buffer::Instance& buffer, size_t body_size,
                                        MessageMetadataSharedPtr metadata) PURE;
  /**
   * deserialize result of an rpc call
   * If successful, the RpcResult removed from the buffer
   *
   * @param buffer the currently buffered dubbo data
   * @body_size the complete RpcResult size
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual RpcResultPtr deserializeRpcResult(Buffer::Instance& buffer, size_t body_size) PURE;

  /**
   * serialize result of an rpc call
   * If successful, the output_buffer is written to the serialized data
   *
   * @param output_buffer store the serialized data
   * @param content the rpc response content
   * @param type the rpc response type
   * @return size_t the length of the serialized content
   */
  virtual size_t serializeRpcResult(Buffer::Instance& output_buffer, const std::string& content,
                                    RpcResponseType type) PURE;
};

using DeserializerPtr = std::unique_ptr<Deserializer>;

/**
 * Implemented by each Dubbo deserialize and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedDeserializerConfigFactory {
public:
  virtual ~NamedDeserializerConfigFactory() = default;

  /**
   * Create a particular Dubbo deserializer.
   * @return DeserializerPtr the transport
   */
  virtual DeserializerPtr createDeserializer() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of Dubbo deserializer
   * produced by the factory.
   */
  virtual std::string name() PURE;

  /**
   * Convenience method to lookup a factory by type.
   * @param TransportType the transport type
   * @return NamedDeserializerConfigFactory& for the TransportType
   */
  static NamedDeserializerConfigFactory& getFactory(SerializationType type) {
    const std::string& name = DeserializerNames::get().fromType(type);
    return Envoy::Config::Utility::getAndCheckFactory<NamedDeserializerConfigFactory>(name);
  }
};

/**
 * DeserializerFactoryBase provides a template for a trivial NamedDeserializerConfigFactory.
 */
template <class DeserializerImpl>
class DeserializerFactoryBase : public NamedDeserializerConfigFactory {
  DeserializerPtr createDeserializer() override { return std::make_unique<DeserializerImpl>(); }

  std::string name() override { return name_; }

protected:
  DeserializerFactoryBase(SerializationType type)
      : name_(DeserializerNames::get().fromType(type)) {}

private:
  const std::string name_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
