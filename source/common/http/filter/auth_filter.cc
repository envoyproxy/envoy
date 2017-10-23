#include "common/http/filter/auth_filter.h"

#include "common/common/assert.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {

FilterHeadersStatus AuthFilter::decodeHeaders(HeaderMap&, bool) {
  /*
RELEASE_ASSERT(callbacks_->route() != nullptr);
RELEASE_ASSERT(callbacks_->route()->routeEntry() != nullptr);

for (const auto auth_config : {callbacks_->route()->routeEntry()->virtualHost().authConfig(),
                             callbacks_->route()->routeEntry()->authConfig()}) {
if (auth_config->x509().valid()) {
  RELEASE_ASSERT(callbacks_->connection() != nullptr);

  auto ssl_connection = callbacks_->connection()->ssl();

  if (ssl_connection == nullptr) {
    Http::Utility::sendLocalReply(*callbacks_, stream_reset_, Code::Unauthorized,
                                  "ssl required");
    return FilterHeadersStatus::StopIteration;
  }

  const auto x509 = auth_config->x509().value();

  if ((x509->sha256Hashes().valid() &&
       x509->sha256Hashes().value().find(ssl_connection->sha256PeerCertificateDigest()) ==
           x509->sha256Hashes().value().end()) ||
      (x509->subjects().valid() &&
       x509->subjects().value().find(ssl_connection->subjectPeerCertificate()) ==
           x509->subjects().value().end()) ||
      (x509->subjectAltNames().valid() &&
       x509->subjectAltNames().value().find(ssl_connection->uriSanPeerCertificate()) ==
           x509->subjectAltNames().value().end())) {
    Http::Utility::sendLocalReply(
        *callbacks_, stream_reset_, Code::Unauthorized,
        fmt::format("ceritificate sha256={}/subject={}/subjectAltName={} not allowed",
                    ssl_connection->sha256PeerCertificateDigest(),
                    ssl_connection->subjectPeerCertificate(),
                    ssl_connection->uriSanPeerCertificate()));
    return FilterHeadersStatus::StopIteration;
  }
}
}

  */
  return FilterHeadersStatus::Continue;
}

} // namespace Http
} // namespace Envoy
