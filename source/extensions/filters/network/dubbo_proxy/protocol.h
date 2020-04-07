#pragma once

#include <string>
#include <unordered_map>

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/network/dubbo_proxy/message.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * See https://dubbo.incubator.apache.org/en-us/docs/dev/implementation.html
 */
class Protocol {
public:
  virtual ~Protocol() = default;
  Protocol() = default;

  /**
   * @return Initializes the serializer used by the protocol codec
   */
  void initSerializer(SerializationType type) {
    serializer_ = NamedSerializerConfigFactory::getFactory(this->type(), type).createSerializer();
  }

  /**
   * @return Serializer the protocol Serializer
   */
  virtual Serializer* serializer() const { return serializer_.get(); }

  virtual const std::string& name() const PURE;

  /**
   * @return ProtocolType the protocol type
   */
  virtual ProtocolType type() const PURE;

  /*
   * decodes the dubbo protocol message header.
   *
   * @param buffer the currently buffered dubbo data.
   * @param metadata the meta data of current messages
   * @return ContextSharedPtr save the context data of current messages,
   *                 nullptr if more data is required.
   *         bool true if a complete message was successfully consumed, false if more data
   *                 is required.
   * @throws EnvoyException if the data is not valid for this protocol.
   */
  virtual std::pair<ContextSharedPtr, bool> decodeHeader(Buffer::Instance& buffer,
                                                         MessageMetadataSharedPtr metadata) PURE;

  /*
   * decodes the dubbo protocol message body, potentially invoking callbacks.
   * If successful, the message is removed from the buffer.
   *
   * @param buffer the currently buffered dubbo data.
   * @param context save the meta data of current messages.
   * @param metadata the meta data of current messages
   * @return bool true if a complete message was successfully consumed, false if more data
   *                 is required.
   * @throws EnvoyException if the data is not valid for this protocol.
   */
  virtual bool decodeData(Buffer::Instance& buffer, ContextSharedPtr context,
                          MessageMetadataSharedPtr metadata) PURE;

  /*
   * encodes the dubbo protocol message.
   *
   * @param buffer save the currently buffered dubbo data.
   * @param metadata the meta data of dubbo protocol
   * @param content the body of dubbo protocol message
   * @param type the type of dubbo protocol response message
   * @return bool true if the protocol coding succeeds.
   */
  virtual bool encode(Buffer::Instance& buffer, const MessageMetadata& metadata,
                      const std::string& content,
                      RpcResponseType type = RpcResponseType::ResponseWithValue) PURE;

protected:
  SerializerPtr serializer_;
};

using ProtocolPtr = std::unique_ptr<Protocol>;

/**
 * Implemented by each Dubbo protocol and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedProtocolConfigFactory : public Config::UntypedFactory {
public:
  ~NamedProtocolConfigFactory() override = default;

  /**
   * Create a particular Dubbo protocol.
   * @param serialization_type the serialization type of the protocol body.
   * @return protocol instance pointer.
   */
  virtual ProtocolPtr createProtocol(SerializationType serialization_type) PURE;

  std::string category() const override { return "envoy.dubbo_proxy.protocols"; }

  /**
   * Convenience method to lookup a factory by type.
   * @param ProtocolType the protocol type.
   * @return NamedProtocolConfigFactory& for the ProtocolType.
   */
  static NamedProtocolConfigFactory& getFactory(ProtocolType type) {
    const std::string& name = ProtocolNames::get().fromType(type);
    return Envoy::Config::Utility::getAndCheckFactoryByName<NamedProtocolConfigFactory>(name);
  }
};

/**
 * ProtocolFactoryBase provides a template for a trivial NamedProtocolConfigFactory.
 */
template <class ProtocolImpl> class ProtocolFactoryBase : public NamedProtocolConfigFactory {
public:
  ProtocolPtr createProtocol(SerializationType serialization_type) override {
    auto protocol = std::make_unique<ProtocolImpl>();
    protocol->initSerializer(serialization_type);
    return protocol;
  }

  std::string name() const override { return name_; }

protected:
  ProtocolFactoryBase(ProtocolType type) : name_(ProtocolNames::get().fromType(type)) {}

private:
  const std::string name_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
