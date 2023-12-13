#pragma once

#include <utility>

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/signer.h"
#include "source/extensions/common/aws/signer_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class SigV4ASignatureHeaderValues : public SignatureHeaderValues {
public:
  const Http::LowerCaseString RegionSet{"x-amz-region-set"};
};

using SigV4ASignatureHeaders = ConstSingleton<SigV4ASignatureHeaderValues>;

class SigV4ASignatureConstantValues : public SignatureConstantValues {
public:
  const std::string SigV4AAuthorizationHeaderFormat{
      "AWS4-ECDSA-P256-SHA256 Credential={}/{}, SignedHeaders={}, Signature={}"};
  const std::string SigV4ACredentialScopeFormat{"{}/{}/aws4_request"};
  const std::string HashedEmptyString{
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"};
  const std::string SigV4ASignatureVersion{"AWS4A"};
  const std::string SigV4AStringToSignFormat{"AWS4-ECDSA-P256-SHA256\n{}\n{}\n{}"};
  const std::string SigV4ALabel = "AWS4-ECDSA-P256-SHA256";
};

enum SigV4AKeyDerivationResult {
  AkdrSuccess,
  AkdrNextCounter,
  AkdrFailure,
};

using SigV4ASignatureConstants = ConstSingleton<SigV4ASignatureConstantValues>;

using AwsSigningHeaderExclusionVector = std::vector<envoy::type::matcher::v3::StringMatcher>;

/**
 * Implementation of the Signature V4A signing process.
 * See https://docs.aws.amazon.com/general/latest/gr/signature-version-4.html
 */

class SigV4ASignerImpl : public SignerBase {
public:
  SigV4ASignerImpl(absl::string_view service_name, absl::string_view region,
                   const CredentialsProviderSharedPtr& credentials_provider,
                   TimeSource& time_source, const AwsSigningHeaderExclusionVector& matcher_config)
      : SignerBase(service_name, region, credentials_provider, time_source, matcher_config) {}

private:
  void addRegionHeader(Http::RequestHeaderMap& headers,
                       const absl::string_view override_region) const override;

  std::string createCredentialScope(const absl::string_view short_date,
                                    const absl::string_view override_region) const override;

  std::string createStringToSign(const absl::string_view canonical_request,
                                 const absl::string_view long_date,
                                 const absl::string_view credential_scope) const override;

  std::string
  createSignature(const absl::string_view access_key_id, const absl::string_view secret_access_key,
                  ABSL_ATTRIBUTE_UNUSED const absl::string_view short_date,
                  const absl::string_view string_to_sign,
                  ABSL_ATTRIBUTE_UNUSED const absl::string_view override_region) const override;

  std::string createAuthorizationHeader(const absl::string_view access_key_id,
                                        const absl::string_view credential_scope,
                                        const std::map<std::string, std::string>& canonical_headers,
                                        const absl::string_view signature) const override;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
