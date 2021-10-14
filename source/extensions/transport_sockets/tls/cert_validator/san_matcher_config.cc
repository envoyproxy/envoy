#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher_config.h"

#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/ssl/certificate_validation_context_config.h"

#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Envoy::Ssl::SanMatcherPtr DnsSanMatcherFactory::createSanMatcher(const Protobuf::Message* config) {
  auto san_matcher =
      dynamic_cast<envoy::extensions::transport_sockets::tls::v3::DnsSanMatcher const*>(config);
  ASSERT(san_matcher != nullptr);
  return Envoy::Ssl::SanMatcherPtr{std::make_unique<DnsSanMatcher>(san_matcher->matcher())};
}

Envoy::Ssl::SanMatcherPtr
EmailSanMatcherFactory::createSanMatcher(const Protobuf::Message* config) {
  auto san_matcher =
      dynamic_cast<envoy::extensions::transport_sockets::tls::v3::EmailSanMatcher const*>(config);
  ASSERT(san_matcher != nullptr);
  return Envoy::Ssl::SanMatcherPtr{std::make_unique<EmailSanMatcher>(san_matcher->matcher())};
}

Envoy::Ssl::SanMatcherPtr UriSanMatcherFactory::createSanMatcher(const Protobuf::Message* config) {
  auto san_matcher =
      dynamic_cast<envoy::extensions::transport_sockets::tls::v3::UriSanMatcher const*>(config);
  ASSERT(san_matcher != nullptr);
  return Envoy::Ssl::SanMatcherPtr{std::make_unique<UriSanMatcher>(san_matcher->matcher())};
}

Envoy::Ssl::SanMatcherPtr
IpAddSanMatcherFactory::createSanMatcher(const Protobuf::Message* config) {
  auto san_matcher =
      dynamic_cast<envoy::extensions::transport_sockets::tls::v3::IpAddSanMatcher const*>(config);
  ASSERT(san_matcher != nullptr);
  return Envoy::Ssl::SanMatcherPtr{std::make_unique<IpAddSanMatcher>(san_matcher->matcher())};
}

bool BackwardsCompatibleSanMatcher::match(const GENERAL_NAMES* general_names) const {
  for (const GENERAL_NAME* general_name : general_names) {
    if (DefaultCertValidator::verifySubjectAltName(general_name, matcher_)) {
      return true;
    }
  }
  return false;
}

Envoy::Ssl ::SanMatcherPtr createBackwardsCompatibleSanMatcher(
    envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher const& matcher) {
  ASSERT(matcher.typed_config().Is<envoy::type::matcher::v3::StringMatcher>());

  envoy::type::matcher::v3::StringMatcher string_matcher;
  matcher.typed_config().MessageUtil::unpackTo(&string_matcher);
  return Envoy::Ssl::SanMatcherPtr{std::make_unique<BackwardsCompatibleSanMatcher>(string_matcher)};
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
