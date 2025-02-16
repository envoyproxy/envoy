#include "source/common/proxy_protocol/proxy_protocol.h"

namespace Envoy {
namespace Common {
namespace ProxyProtocol {

Network::ProxyProtocolTLVVector
parseTLVs(absl::Span<const envoy::config::core::v3::TlvEntry* const> tlvs) {
  Network::ProxyProtocolTLVVector tlv_vector;
  for (const auto& tlv : tlvs) {
    tlv_vector.push_back({static_cast<uint8_t>(tlv->type()),
                          std::vector<unsigned char>(tlv->value().begin(), tlv->value().end())});
  }
  return tlv_vector;
}

} // namespace ProxyProtocol
} // namespace Common
} // namespace Envoy
