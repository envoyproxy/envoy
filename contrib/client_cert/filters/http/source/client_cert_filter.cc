#include "contrib/client_cert/filters/http/source/client_cert_filter.h"

#include <string>
#include <vector>

#include "envoy/http/header_map.h"
#include "envoy/ssl/connection.h"

#include "source/common/common/macros.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ClientCert {

namespace {

const Http::LowerCaseString& clientCertHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "client-cert");
}

const Http::LowerCaseString& clientCertChainHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "client-cert-chain");
}

// RFC 9440 section 2.1: base64 DER wrapped in colons, i.e. the PEM body with the
// encapsulation boundaries and line breaks removed.
std::string pemToRfc9440ByteSequence(absl::string_view pem) {
  std::string der_base64(pem);
  absl::StrReplaceAll(
      {
          {"-----BEGIN CERTIFICATE-----", ""},
          {"-----END CERTIFICATE-----", ""},
          {"\n", ""},
          {"\r", ""},
      },
      &der_base64);
  return absl::StrCat(":", der_base64, ":");
}

} // namespace

Http::FilterHeadersStatus ClientCertFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // RFC 9440 section 2.4: incoming occurrences of these headers must always be removed
  // before forwarding, on any connection type.
  headers.remove(clientCertHeader());
  headers.remove(clientCertChainHeader());

  const OptRef<const Network::Connection> connection = decoder_callbacks_->connection();
  if (!connection.has_value() || connection->ssl() == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }
  const Ssl::ConnectionInfoConstSharedPtr ssl = connection->ssl();

  const std::string& leaf_pem = ssl->pemEncodedPeerCertificate();
  if (leaf_pem.empty()) {
    return Http::FilterHeadersStatus::Continue;
  }
  headers.addCopy(clientCertHeader(), pemToRfc9440ByteSequence(leaf_pem));

  // RFC 9440 section 2.3: the chain excludes the end-entity certificate, which is element 0
  // of the leaf-first validated chain.
  if (!config_->setClientCertChain()) {
    return Http::FilterHeadersStatus::Continue;
  }
  const absl::Span<const std::string> chain = ssl->pemEncodedValidatedPeerCertificateChain();
  if (chain.size() <= 1) {
    return Http::FilterHeadersStatus::Continue;
  }
  std::vector<std::string> encoded_chain;
  encoded_chain.reserve(chain.size() - 1);
  for (const std::string& cert_pem : chain.subspan(1)) {
    encoded_chain.push_back(pemToRfc9440ByteSequence(cert_pem));
  }
  headers.addCopy(clientCertChainHeader(), absl::StrJoin(encoded_chain, ", "));

  return Http::FilterHeadersStatus::Continue;
}

} // namespace ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
