#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/udp_packet_writer_handler.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Network {

class UdpPacketWriterConfigFactory : public Config::UntypedFactory {
public:
  ~UdpPacketWriterConfigFactory() override = default;

  /**
   * @brief Create a Empty Config Proto object which can be used
   * for UdpPacketWriter creation
   * @return ProtobufTypes::MessagePtr
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * Create an UdpPacketWriterFactory object according to given message.
   * @param message specifies Udp Packet Writer options in a protobuf.
   */
  virtual Network::UdpPacketWriterFactoryPtr
  createUdpPacketWriterFactory(const Protobuf::Message& message) PURE;

  std::string category() const override { return "envoy.udp_packet_writers"; }
};

} // namespace Network
} // namespace Envoy
