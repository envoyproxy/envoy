#include "source/extensions/filters/common/rbac/principals/mtls_authenticated/mtls_authenticated.h"

#include "envoy/extensions/rbac/principals/mtls_authenticated/v3/mtls_authenticated.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Principals {

MtlsAuthenticatedMatcher::MtlsAuthenticatedMatcher(
    const envoy::extensions::rbac::principals::mtls_authenticated::v3::Config& auth,
    Server::Configuration::CommonFactoryContext& context)
    : matcher_(auth.has_san_matcher() ? Extensions::TransportSockets::Tls::createStringSanMatcher(
                                            auth.san_matcher(), context)
                                      : nullptr) {
  if (!auth.has_san_matcher() && !auth.any_validated_client_certificate()) {
    throw EnvoyException("envoy.rbac.principals.mtls_authenticated did not have any configured "
                         "validation. At least one configuration field must be set.");
  }
}

bool MtlsAuthenticatedMatcher::matches(const Network::Connection& connection,
                                       const Envoy::Http::RequestHeaderMap&,
                                       const StreamInfo::StreamInfo&) const {
  const auto& ssl = connection.ssl();
  if (!ssl) { // connection was not authenticated
    return false;
  }

  if (!ssl->peerCertificateValidated()) {
    return false;
  }

  if (matcher_ == nullptr) {
    return true;
  }

  return ssl->peerCertificateSanMatches(*matcher_);
}

class Factory : public Filters::Common::RBAC::BasePrincipalExtensionFactory<
                    MtlsAuthenticatedMatcher,
                    envoy::extensions::rbac::principals::mtls_authenticated::v3::Config> {
public:
  std::string name() const override { return "envoy.rbac.principals.mtls_authenticated"; }
};

REGISTER_FACTORY(Factory, Filters::Common::RBAC::PrincipalExtensionFactory);

} // namespace Principals
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
