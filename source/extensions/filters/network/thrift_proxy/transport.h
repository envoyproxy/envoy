#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/network/thrift_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Transport represents a Thrift transport. The Thrift transport is nominally a generic,
 * bi-directional byte stream. In Envoy we assume it always represents a network byte stream and
 * the Transport is just a mechanism for framing messages and passing message metadata.
 */
class Transport {
public:
  virtual ~Transport() = default;

  /*
   * Returns this transport's name.
   *
   * @return std::string containing the transport name.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return TransportType the transport type
   */
  virtual TransportType type() const PURE;

  /*
   * Decodes the start of a transport message. If successful, the start of the frame is removed
   * from the buffer. Transports should not modify the buffer, headers, protocol type, or size if
   * more data is required to decode the frame's start. If the full frame start can be decoded, the
   * Transport must drain the frame start data from the buffer. The request metadata should be
   * modified with any data available to the transport.
   *
   * @param buffer the currently buffered thrift data.
   * @param metadata MessageMetadata to be modified if transport supports additional information
   * @return bool true if a complete frame header was successfully consumed, false if more data
   *                 is required.
   * @throws EnvoyException if the data is not valid for this transport.
   */
  virtual bool decodeFrameStart(Buffer::Instance& buffer, MessageMetadata& metadata) PURE;

  /*
   * Decodes the end of a transport message. If successful, the end of the frame is removed from
   * the buffer.
   *
   * @param buffer the currently buffered thrift data.
   * @return bool true if a complete frame trailer was successfully consumed, false if more data
   *                 is required.
   * @throws EnvoyException if the data is not valid for this transport.
   */
  virtual bool decodeFrameEnd(Buffer::Instance& buffer) PURE;

  /**
   * Wraps the given message buffer with the transport's header and trailer (if any). After
   * encoding, message will be empty.
   * @param buffer is the output buffer
   * @param metadata MessageMetadata for the message
   * @param message a protocol-encoded message
   * @throws EnvoyException if the message is too large for the transport
   */
  virtual void encodeFrame(Buffer::Instance& buffer, const MessageMetadata& metadata,
                           Buffer::Instance& message) PURE;
};

using TransportPtr = std::unique_ptr<Transport>;

/**
 * Implemented by each Thrift transport and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedTransportConfigFactory : public Envoy::Config::UntypedFactory {
public:
  ~NamedTransportConfigFactory() override = default;

  /**
   * Create a particular Thrift transport.
   * @return TransportPtr the transport
   */
  virtual TransportPtr createTransport() PURE;

  std::string category() const override { return "envoy.thrift_proxy.transports"; }

  /**
   * Convenience method to lookup a factory by type.
   * @param TransportType the transport type
   * @return NamedTransportConfigFactory& for the TransportType
   */
  static NamedTransportConfigFactory& getFactory(TransportType type) {
    const std::string& name = TransportNames::get().fromType(type);
    return Envoy::Config::Utility::getAndCheckFactoryByName<NamedTransportConfigFactory>(name);
  }
};

/**
 * TransportFactoryBase provides a template for a trivial NamedTransportConfigFactory.
 */
template <class TransportImpl> class TransportFactoryBase : public NamedTransportConfigFactory {
public:
  TransportPtr createTransport() override { return std::move(std::make_unique<TransportImpl>()); }

  std::string name() const override { return name_; }

protected:
  TransportFactoryBase(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
