#include "contrib/generic_proxy/filters/network/test/fake_codec.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

RequestDecoderPtr FakeStreamCodecFactory::requestDecoder() const {
  return std::make_unique<FakeRequestDecoder>();
}

ResponseDecoderPtr FakeStreamCodecFactory::responseDecoder() const {
  return std::make_unique<FakeResponseDecoder>();
}
RequestEncoderPtr FakeStreamCodecFactory::requestEncoder() const {
  return std::make_unique<FakeRequestEncoder>();
}
ResponseEncoderPtr FakeStreamCodecFactory::responseEncoder() const {
  return std::make_unique<FakeResponseEncoder>();
}
MessageCreatorPtr FakeStreamCodecFactory::messageCreator() const {
  return std::make_unique<FakeMessageCreator>();
}
ProtocolOptions FakeStreamCodecFactory::protocolOptions() const { return protocol_options_; }

CodecFactoryPtr
FakeStreamCodecFactoryConfig::createCodecFactory(const Protobuf::Message&,
                                                 Envoy::Server::Configuration::FactoryContext&) {
  auto factory = std::make_unique<FakeStreamCodecFactory>();
  factory->protocol_options_ = protocol_options_;
  return factory;
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
