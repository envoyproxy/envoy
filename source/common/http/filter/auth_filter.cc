#include "common/http/filter/auth_filter.h"

#include "common/common/assert.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {

FilterHeadersStatus AuthFilter::decodeHeaders(HeaderMap&, bool) {
  RELEASE_ASSERT(callbacks_->route() != nullptr);
  RELEASE_ASSERT(callbacks_->route()->routeEntry() != nullptr);

  for (const auto auth_config : {callbacks_->route()->routeEntry()->virtualHost().authConfig(),
                                 callbacks_->route()->routeEntry()->authConfig()}) {
    if (auth_config->x509().valid()) {
      RELEASE_ASSERT(callbacks_->connection() != nullptr);

      auto ssl_connection = callbacks_->connection()->ssl();

      if (ssl_connection == nullptr) {
        Http::Utility::sendLocalReply(*callbacks_, stream_reset_, Code::Unauthorized, "");
        return FilterHeadersStatus::StopIteration;
      }

      const auto x509 = auth_config->x509().value();

      if ((!x509.certificate_hash_.empty() &&
           ssl_connection->sha256PeerCertificateDigest() != x509.certificate_hash_) ||
          (!x509.subjects_.empty() &&
           x509.subjects_.find(ssl_connection->subjectPeerCertificate()) == x509.subjects_.end()) ||
          (!x509.subject_alt_names_.empty() &&
           x509.subject_alt_names_.find(ssl_connection->uriSanPeerCertificate()) ==
               x509.subject_alt_names_.end())) {
        Http::Utility::sendLocalReply(*callbacks_, stream_reset_, Code::Unauthorized, "");
        return FilterHeadersStatus::StopIteration;
      }
    }
  }

  return FilterHeadersStatus::Continue;
}

} // namespace Http
} // namespace Envoy
