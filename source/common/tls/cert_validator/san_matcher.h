#pragma once

#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/hash.h"
#include "source/common/common/matchers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tls/utility.h"

#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

/**
 * Interface to verify if there is a match in a list of subject alternative
 * names.
 */
class SanMatcher {
public:
  virtual bool match(GENERAL_NAME const*) const PURE;
  virtual bool match(GENERAL_NAME const* general_name, const StreamInfo::StreamInfo&) const {
    return match(general_name);
  }
  virtual ~SanMatcher() = default;
};

using SanMatcherPtr = std::unique_ptr<SanMatcher>;
} // namespace Ssl

namespace Extensions {
namespace TransportSockets {
namespace Tls {

using Ssl::SanMatcher;
using Ssl::SanMatcherPtr;

class StringSanMatcher : public SanMatcher {
public:
  bool match(const GENERAL_NAME* general_name) const override;
  bool match(const GENERAL_NAME* general_name,
             const StreamInfo::StreamInfo& stream_info) const override;
  ~StringSanMatcher() override = default;

  StringSanMatcher(int general_name_type, envoy::type::matcher::v3::StringMatcher matcher,
                   Server::Configuration::CommonFactoryContext& context,
                   bssl::UniquePtr<ASN1_OBJECT>&& general_name_oid = nullptr)
      : general_name_type_(general_name_type), matcher_(matcher, context),
        general_name_oid_(std::move(general_name_oid)) {
    // For DNS SAN, if the StringMatcher type is exact, we have to follow DNS matching semantics.
    // The DnsStringSanMatcher should be used in this case.
    ASSERT(general_name_type != GEN_DNS ||
           matcher.match_pattern_case() !=
               envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kExact);
  }

private:
  bool typeCompatible(const GENERAL_NAME* general_name) const;

  const int general_name_type_;
  const Envoy::Matchers::StringMatcherImpl matcher_;
  bssl::UniquePtr<ASN1_OBJECT> general_name_oid_;
};

// A DNS string SAN matcher that uses the dnsNameMatch() function.
// This should be used for DNS SAN where the StringMatcher type is exact,
// and the DNS matching semantics must be followed.
class DnsExactStringSanMatcher : public SanMatcher {
public:
  bool match(const GENERAL_NAME* general_name) const override;
  ~DnsExactStringSanMatcher() override = default;

  DnsExactStringSanMatcher(absl::string_view dns_exact_match) : dns_exact_match_(dns_exact_match) {}

private:
  const std::string dns_exact_match_;
};

SanMatcherPtr createStringSanMatcher(
    const envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher& matcher,
    Server::Configuration::CommonFactoryContext& context);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
