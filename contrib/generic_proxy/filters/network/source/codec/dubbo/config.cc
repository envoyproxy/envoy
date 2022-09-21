#include "contrib/generic_proxy/filters/network/source/codec/dubbo/config.h"

#include "envoy/registry/registry.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Dubbo {

void GenericRequest::forEach(IterateCallback callback) const {}
absl::optional<absl::string_view> GenericRequest::getByKey(absl::string_view key) const {}
void GenericRequest::setByKey(absl::string_view key, absl::string_view val) {}
void GenericRequest::setByReferenceKey(absl::string_view key, absl::string_view val) {}
void GenericRequest::setByReference(absl::string_view key, absl::string_view val) {}

CodecFactoryPtr
DubboCodecFactoryConfig::createFactory(const Protobuf::Message&,
                                       Envoy::Server::Configuration::FactoryContext&) {
  return std::make_unique<DubboCodecFactory>();
}

REGISTER_FACTORY(DubboCodecFactoryConfig, CodecFactoryConfig);

} // namespace Dubbo
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
