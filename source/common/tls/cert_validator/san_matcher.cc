#include "source/common/tls/cert_validator/san_matcher.h"

#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/ssl/certificate_validation_context_config.h"

#include "source/common/tls/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

bool StringSanMatcher::typeCompatible(const GENERAL_NAME* general_name) const {
  if (general_name->type != general_name_type_) {
    return false;
  }
  if (general_name->type == GEN_OTHERNAME) {
    if (OBJ_cmp(general_name->d.otherName->type_id, general_name_oid_.get())) {
      return false;
    }
  }

  return true;
}

bool StringSanMatcher::match(const GENERAL_NAME* general_name) const {
  if (!typeCompatible(general_name)) {
    return false;
  }

  return matcher_.match(Utility::generalNameAsString(general_name));
}

bool StringSanMatcher::match(const GENERAL_NAME* general_name,
                             const StreamInfo::StreamInfo& stream_info) const {
  if (!typeCompatible(general_name)) {
    return false;
  }

  Matchers::StringMatcher::Context context{makeOptRef(stream_info)};
  return matcher_.match(Utility::generalNameAsString(general_name), context);
}

bool DnsExactStringSanMatcher::match(const GENERAL_NAME* general_name) const {
  if (general_name->type != GEN_DNS) {
    return false;
  }
  return Utility::dnsNameMatch(dns_exact_match_, Utility::generalNameAsString(general_name));
}

SanMatcherPtr createStringSanMatcher(
    envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher const& matcher,
    Server::Configuration::CommonFactoryContext& context) {
  // Verify that a new san type has not been added.
  static_assert(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SanType_MAX ==
                5);

  switch (matcher.san_type()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS:
    // For DNS SAN, if the StringMatcher type is exact, we have to follow DNS matching semantics.
    if (matcher.matcher().match_pattern_case() ==
        envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kExact) {
      return SanMatcherPtr{std::make_unique<DnsExactStringSanMatcher>(matcher.matcher().exact())};
    } else {
      return SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher.matcher(), context)};
    }
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::EMAIL:
    return SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_EMAIL, matcher.matcher(), context)};
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI:
    return SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_URI, matcher.matcher(), context)};
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::IP_ADDRESS:
    return SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_IPADD, matcher.matcher(), context)};
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::OTHER_NAME: {
    // Invalid/Empty OID returns a nullptr from OBJ_txt2obj
    bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj(matcher.oid().c_str(), 0));
    if (oid == nullptr) {
      return nullptr;
    }
    return SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher.matcher(),
                                                            context, std::move(oid))};
  }
  case envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SAN_TYPE_UNSPECIFIED:
    PANIC("unhandled value");
  }
  return nullptr;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
