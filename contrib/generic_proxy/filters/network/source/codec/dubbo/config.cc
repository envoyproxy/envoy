#include "contrib/generic_proxy/filters/network/source/codec/dubbo/config.h"
#include "source/extensions/common/dubbo/message_impl.h"

#include "envoy/registry/registry.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Dubbo {

void GenericRequest::forEach(IterateCallback callback) const {
  ASSERT(dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_request_->mutableRequest()) !=
         nullptr);
  auto* typed_request =
      dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_request_->mutableRequest());
  // TODO(wbpcode): better attachment structure is necessary to simplify these code the improve
  // performance.
  typed_request->attachment().headers().iterate(
      [cb = std::move(callback)](const Http::HeaderEntry& header) {
        if (cb(header.key().getStringView(), header.value().getStringView())) {
          return Http::HeaderMap::Iterate::Continue;
        } else {
          return Http::HeaderMap::Iterate::Break;
        }
      });
}
absl::optional<absl::string_view> GenericRequest::getByKey(absl::string_view key) const {
  ASSERT(dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_request_->mutableRequest()) !=
         nullptr);
  auto* typed_request =
      dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_request_->mutableRequest());

  auto* result = typed_request->attachment().lookup(std::string(key));
  if (result == nullptr) {
    return absl::nullopt;
  }
  return absl::make_optional<absl::string_view>(*result);
}
void GenericRequest::setByKey(absl::string_view key, absl::string_view val) {
  ASSERT(dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_request_->mutableRequest()) !=
         nullptr);
  auto* typed_request =
      dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_request_->mutableRequest());
  typed_request->mutableAttachment()->insert(std::string(key), std::string(val));
}

void GenericRespnose::refreshGenericStatus() {

}

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
