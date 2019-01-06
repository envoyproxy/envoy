#pragma once

#include <string>

#include "envoy/quic/listener.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Quic {

/**
 * Implemented by the QUIC listener and registered via RegisterFactory.
 */
class QuicListenerConfigFactory {
public:
  virtual ~QuicListenerConfigFactory() {}

  /**
   * Create a particular QUIC listener factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException.
   * @param config the protobuf configuration for the listener.
   * @param context the listener's context.
   */
  virtual QuicListenerFactoryPtr
  createListenerFactoryFromProto(const Protobuf::Message& config,
                                 Server::Configuration::ListenerFactoryContext& context) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message. The QUIC listener
   *         config, which arrives in an opaque message, will be parsed into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a QUIC listener
   * produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace Quic
} // namespace Envoy
