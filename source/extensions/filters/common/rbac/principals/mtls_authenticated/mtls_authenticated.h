#pragma once

#include "envoy/extensions/rbac/principals/mtls_authenticated/v3/mtls_authenticated.pb.h"
#include "source/extensions/filters/common/rbac/matchers.h"
#include "source/extensions/filters/common/rbac/principal_extension.h"
#include "source/common/tls/cert_validator/san_matcher.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Principals {

class MtlsAuthenticatedMatcher : public RBAC::Matcher {
public:
  MtlsAuthenticatedMatcher(
      const envoy::extensions::rbac::principals::mtls_authenticated::v3::Config& auth,
      Server::Configuration::CommonFactoryContext& context);

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const Ssl::SanMatcherPtr matcher_;
};

} // namespace Principals
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
