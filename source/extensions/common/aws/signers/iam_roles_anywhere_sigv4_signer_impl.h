#pragma once
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/iam_roles_anywhere_signer_base_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

using IAMRolesAnywhereSigV4SignatureHeaders = ConstSingleton<IAMRolesAnywhereSignatureHeaderValues>;

class IAMRolesAnywhereSigV4SignatureConstants : public IAMRolesAnywhereSignatureConstants {
public:
  static constexpr absl::string_view SigV4AuthorizationHeaderFormat{
      "{} Credential={}, SignedHeaders={}, Signature={}"};
  static constexpr absl::string_view SigV4CredentialScopeFormat{"{}/{}/{}/aws4_request"};
  static constexpr absl::string_view SigV4StringToSignFormat{"{}\n{}\n{}\n{}"};
  static constexpr absl::string_view X509SigV4RSA{"AWS4-X509-RSA-SHA256"};
  static constexpr absl::string_view X509SigV4ECDSA{"AWS4-X509-ECDSA-SHA256"};
};

/*
 * Implementation of the Signature V4 signing process using X509 Credentials for IAM Roles Anywhere.
 * See https://docs.aws.amazon.com/rolesanywhere/latest/userguide/authentication-sign-process.html
 */
class IAMRolesAnywhereSigV4Signer : public IAMRolesAnywhereSignerBaseImpl {

public:
  IAMRolesAnywhereSigV4Signer(absl::string_view service_name, absl::string_view region,
                              const X509CredentialsProviderSharedPtr& credentials_provider,
                              TimeSource& timesource)
      : IAMRolesAnywhereSignerBaseImpl(service_name, region, credentials_provider, timesource) {}

private:
  std::string createCredentialScope(const absl::string_view short_date,
                                    const absl::string_view override_region) const override;

  absl::StatusOr<std::string>
  createSignature(const X509Credentials& x509_credentials,
                  const absl::string_view string_to_sign) const override;

  std::string createAuthorizationHeader(const X509Credentials& x509_credentials,
                                        const absl::string_view credential_scope,
                                        const std::map<std::string, std::string>& canonical_headers,
                                        const absl::string_view signature) const override;

  std::string createStringToSign(const X509Credentials& x509_credentials,
                                 const absl::string_view canonical_request,
                                 const absl::string_view long_date,
                                 const absl::string_view credential_scope) const override;

  absl::string_view getAlgorithmName(const X509Credentials& x509_credentials) const;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
