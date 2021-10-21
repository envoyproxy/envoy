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

Envoy::Ssl::SanMatcherPtr BackwardsCompatibleSanMatcherFactory::createSanMatcher(
    const envoy::config::core::v3::TypedExtensionConfig* config) {
  ASSERT(config->typed_config().Is<envoy::type::matcher::v3::StringMatcher>());
  envoy::type::matcher::v3::StringMatcher string_matcher;
  MessageUtil::unpackTo(config->typed_config(), string_matcher);
  return Envoy::Ssl::SanMatcherPtr{std::make_unique<BackwardsCompatibleSanMatcher>(string_matcher)};
}

bool BackwardsCompatibleSanMatcher::match(const GENERAL_NAME* general_name) const {
  return DefaultCertValidator::verifySubjectAltName(general_name, matcher_);
}

Envoy::Ssl::SanMatcherPtr createStringSanMatcher(
    envoy::extensions::transport_sockets::tls::v3::StringSanMatcher const& matcher) {
  switch (matcher.san_type()) {
  case envoy::extensions::transport_sockets::tls::v3::StringSanMatcher::DNS_ID: {
    return Envoy::Ssl::SanMatcherPtr{std::make_unique<DnsSanMatcher>(matcher.matcher())};
  }
  case envoy::extensions::transport_sockets::tls::v3::StringSanMatcher::EMAIL_ID: {
    return Envoy::Ssl::SanMatcherPtr{std::make_unique<EmailSanMatcher>(matcher.matcher())};
  }
  case envoy::extensions::transport_sockets::tls::v3::StringSanMatcher::URI_ID: {
    return Envoy::Ssl::SanMatcherPtr{std::make_unique<UriSanMatcher>(matcher.matcher())};
  }
  case envoy::extensions::transport_sockets::tls::v3::StringSanMatcher::IP_ADD: {
    return Envoy::Ssl::SanMatcherPtr{std::make_unique<IpAddSanMatcher>(matcher.matcher())};
  }
  default:
    ASSERT(true,
           "Invalid san type for envoy::extensions::transport_sockets::tls::v3::StringSanMatcher");
    return Envoy::Ssl::SanMatcherPtr();
  }
}

REGISTER_FACTORY(BackwardsCompatibleSanMatcherFactory, Envoy::Ssl::SanMatcherFactory);
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
