#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher_config.h"

#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/ssl/certificate_validation_context_config.h"

#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Envoy::Ssl::SanMatcherPtr createStringSanMatcher(
    envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher const& matcher) {
  // Verify that a new san type has not been added.
  static_assert(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SanType_MAX ==
                3);

  switch (matcher.san_type()) {
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS:
    return Envoy::Ssl::SanMatcherPtr{std::make_unique<DnsSanMatcher>(matcher.matcher())};
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::EMAIL:
    return Envoy::Ssl::SanMatcherPtr{std::make_unique<EmailSanMatcher>(matcher.matcher())};
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI:
    return Envoy::Ssl::SanMatcherPtr{std::make_unique<UriSanMatcher>(matcher.matcher())};
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::IP_ADDRESS:
    return Envoy::Ssl::SanMatcherPtr{std::make_unique<IpAddSanMatcher>(matcher.matcher())};
  default:
    RELEASE_ASSERT(true, "Invalid san type for "
                         "envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher");
    return Envoy::Ssl::SanMatcherPtr();
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
