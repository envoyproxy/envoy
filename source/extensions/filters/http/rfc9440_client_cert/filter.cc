#include "source/extensions/filters/http/rfc9440_client_cert/filter.h"

#include "envoy/http/header_map.h"

#include "source/common/common/fmt.h"
#include "source/common/http/headers.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Rfc9440ClientCert {

Http::FilterHeadersStatus Rfc9440ClientCertFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                 bool) {
  headers.remove(Http::LowerCaseString("client-cert"));
  headers.remove(Http::LowerCaseString("client-cert-chain"));

  const Network::Connection* connection = decoder_callbacks_->connection().ptr();
  if (!connection || !connection->ssl()) {
    return Http::FilterHeadersStatus::Continue;
  }

  auto ssl = connection->ssl();

  const std::string& leaf_b64 = ssl->b64DerEncodedPeerCertificate();
  if (!leaf_b64.empty()) {
    headers.addCopy(Http::LowerCaseString("client-cert"), fmt::format(":{}:", leaf_b64));
  }

  auto chain_b64 = ssl->b64DerEncodedPeerCertificateChain();
  if (!chain_b64.empty()) {
    std::vector<std::string> formatted_chain;
    formatted_chain.reserve(chain_b64.size());
    for (const auto& cert : chain_b64) {
      formatted_chain.push_back(fmt::format(":{}:", cert));
    }
    headers.addCopy(Http::LowerCaseString("client-cert-chain"),
                    absl::StrJoin(formatted_chain, ", "));
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace Rfc9440ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
