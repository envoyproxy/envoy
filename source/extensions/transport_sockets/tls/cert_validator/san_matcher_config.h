#pragma once

#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/hash.h"
#include "source/common/common/matchers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

template <int general_name_type> class StringSanMatcher : public Envoy::Ssl::SanMatcher {
public:
  bool match(const GENERAL_NAME* general_name) const override {
    return general_name->type == general_name_type &&
           DefaultCertValidator::verifySubjectAltName(general_name, matcher_);
  }

  ~StringSanMatcher() override = default;

  StringSanMatcher(envoy::type::matcher::v3::StringMatcher matcher) : matcher_(matcher) {}

private:
  Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
};

using DnsSanMatcher = StringSanMatcher<GEN_DNS>;
using EmailSanMatcher = StringSanMatcher<GEN_EMAIL>;
using UriSanMatcher = StringSanMatcher<GEN_URI>;
using IpAddSanMatcher = StringSanMatcher<GEN_IPADD>;

Envoy::Ssl::SanMatcherPtr createStringSanMatcher(
    const envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher& matcher);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
