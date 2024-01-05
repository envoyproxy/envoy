#pragma once

#include <utility>

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/signer.h"
#include "source/extensions/common/aws/signer_base_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

using SigV4SignatureHeaders = ConstSingleton<SignatureHeaderValues>;

class SigV4SignatureConstantValues : public SignatureConstantValues {
public:
  const std::string SigV4AuthorizationHeaderFormat{
      "AWS4-HMAC-SHA256 Credential={}/{}, SignedHeaders={}, Signature={}"};
  const std::string SigV4CredentialScopeFormat{"{}/{}/{}/aws4_request"};
  const std::string SigV4SignatureVersion{"AWS4"};
  const std::string SigV4StringToSignFormat{"AWS4-HMAC-SHA256\n{}\n{}\n{}"};
};

using SigV4SignatureConstants = ConstSingleton<SigV4SignatureConstantValues>;

using AwsSigningHeaderExclusionVector = std::vector<envoy::type::matcher::v3::StringMatcher>;

/**
 * Implementation of the Signature V4 signing process.
 * See https://docs.aws.amazon.com/IAM/latest/UserGuide/reference_aws-signing.html
 */
class SigV4SignerImpl : public SignerBaseImpl {
public:
  SigV4SignerImpl(absl::string_view service_name, absl::string_view region,
                  const CredentialsProviderSharedPtr& credentials_provider, TimeSource& time_source,
                  const AwsSigningHeaderExclusionVector& matcher_config)
      : SignerBaseImpl(service_name, region, credentials_provider, time_source, matcher_config) {}

private:
  std::string createCredentialScope(const absl::string_view short_date,
                                    const absl::string_view override_region) const override;

  std::string createStringToSign(const absl::string_view canonical_request,
                                 const absl::string_view long_date,
                                 const absl::string_view credential_scope) const override;

  std::string createSignature(ABSL_ATTRIBUTE_UNUSED const absl::string_view access_key_id,
                              const absl::string_view secret_access_key,
                              const absl::string_view short_date,
                              const absl::string_view string_to_sign,
                              const absl::string_view override_region) const override;

  std::string createAuthorizationHeader(const absl::string_view access_key_id,
                                        const absl::string_view credential_scope,
                                        const std::map<std::string, std::string>& canonical_headers,
                                        const absl::string_view signature) const override;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
