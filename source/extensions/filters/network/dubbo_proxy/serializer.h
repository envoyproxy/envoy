#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/network/dubbo_proxy/message.h"
#include "source/extensions/filters/network/dubbo_proxy/metadata.h"
#include "source/extensions/filters/network/dubbo_proxy/protocol_constants.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class Serializer {
public:
  virtual ~Serializer() = default;

  /**
   * Return this Serializer's name
   *
   * @return std::string containing the serialization name.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return SerializationType the serializer type
   */
  virtual SerializationType type() const PURE;

  /**
   * deserialize an rpc call
   * If successful, the RpcInvocation removed from the buffer
   *
   * @param buffer the currently buffered dubbo data
   * @param context context information for RPC messages
   * @return a pair containing the deserialized result of the message and the deserialized
   *         invocation information.
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual std::pair<RpcInvocationSharedPtr, bool>
  deserializeRpcInvocation(Buffer::Instance& buffer, ContextSharedPtr context) PURE;

  /**
   * deserialize result of an rpc call
   *
   * @param buffer the currently buffered dubbo data
   * @param context context information for RPC messages
   * @return a pair containing the deserialized result of the message and the deserialized
   *         result information.
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual std::pair<RpcResultSharedPtr, bool> deserializeRpcResult(Buffer::Instance& buffer,
                                                                   ContextSharedPtr context) PURE;

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

using SerializerPtr = std::unique_ptr<Serializer>;

/**
 * Implemented by each Dubbo serialize and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedSerializerConfigFactory : public Config::UntypedFactory {
public:
  ~NamedSerializerConfigFactory() override = default;

  /**
   * Create a particular Dubbo serializer.
   * @return SerializerPtr the transport
   */
  virtual SerializerPtr createSerializer() PURE;

  std::string category() const override { return "envoy.dubbo_proxy.serializers"; }

  /**
   * Convenience method to lookup a factory by type.
   * @param TransportType the transport type
   * @return NamedSerializerConfigFactory& for the TransportType
   */
  static NamedSerializerConfigFactory& getFactory(ProtocolType protocol_type,
                                                  SerializationType type) {
    const std::string& name = ProtocolSerializerNames::get().fromType(protocol_type, type);
    return Envoy::Config::Utility::getAndCheckFactoryByName<NamedSerializerConfigFactory>(name);
  }
};

/**
 * SerializerFactoryBase provides a template for a trivial NamedSerializerConfigFactory.
 */
template <class SerializerImpl> class SerializerFactoryBase : public NamedSerializerConfigFactory {
public:
  SerializerPtr createSerializer() override { return std::make_unique<SerializerImpl>(); }

  std::string name() const override { return name_; }

protected:
  SerializerFactoryBase(ProtocolType protocol_type, SerializationType type)
      : name_(ProtocolSerializerNames::get().fromType(protocol_type, type)) {}

private:
  const std::string name_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
