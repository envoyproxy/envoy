#include "test/extensions/filters/network/generic_proxy/fake_codec.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

ServerCodecPtr FakeStreamCodecFactory::createServerCodec() const {
  return std::make_unique<FakeServerCodec>();
}

ClientCodecPtr FakeStreamCodecFactory::createClientCodec() const {
  return std::make_unique<FakeClientCodec>();
}

CodecFactoryPtr FakeStreamCodecFactoryConfig::createCodecFactory(
    const Protobuf::Message&, Envoy::Server::Configuration::ServerFactoryContext&) {
  return std::make_unique<FakeStreamCodecFactory>();
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
