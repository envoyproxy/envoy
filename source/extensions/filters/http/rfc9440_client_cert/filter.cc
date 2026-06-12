#include "source/extensions/filters/http/rfc9440_client_cert/filter.h"

#include "envoy/http/header_map.h"

#include "source/common/common/fmt.h"
#include "source/common/http/headers.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Rfc9440ClientCert {

namespace {
std::string pemToRfc9440Value(absl::string_view pem) {
  std::string result(pem);
  absl::StrReplaceAll(
      {
          {"-----BEGIN CERTIFICATE-----", ""},
          {"-----END CERTIFICATE-----", ""},
          {"\n", ""},
          {"\r", ""},
      },
      &result);
  return result;
}
} // namespace

Http::FilterHeadersStatus Rfc9440ClientCertFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                 bool) {
  headers.remove(Http::LowerCaseString("client-cert"));
  headers.remove(Http::LowerCaseString("client-cert-chain"));

  const Network::Connection* connection = decoder_callbacks_->connection().ptr();
  if (!connection || !connection->ssl()) {
    return Http::FilterHeadersStatus::Continue;
  }

  auto ssl = connection->ssl();

  const std::string& leaf_pem = ssl->pemEncodedPeerCertificate();
  if (!leaf_pem.empty()) {
    std::string leaf_b64 = pemToRfc9440Value(leaf_pem);
    headers.addCopy(Http::LowerCaseString("client-cert"), fmt::format(":{}:", leaf_b64));
  }

  if (config_->setClientCertChain()) {
    auto chain_pem = ssl->pemEncodedPeerCertificateChain();
    if (chain_pem.size() > 1) {
      std::vector<std::string> formatted_chain;
      formatted_chain.reserve(chain_pem.size() - 1);

      for (size_t i = 1; i < chain_pem.size(); ++i) {
        formatted_chain.push_back(fmt::format(":{}:", pemToRfc9440Value(chain_pem[i])));
      }
      headers.addCopy(Http::LowerCaseString("client-cert-chain"),
                      absl::StrJoin(formatted_chain, ", "));
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace Rfc9440ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
