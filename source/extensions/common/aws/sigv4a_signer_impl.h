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

class SigV4ASignatureHeaderValues : public SignatureHeaderValues {
public:
  const Http::LowerCaseString RegionSet{"x-amz-region-set"};
};

using SigV4ASignatureHeaders = ConstSingleton<SigV4ASignatureHeaderValues>;

class SigV4ASignatureConstants : public SignatureConstants {
public:
  static constexpr absl::string_view SigV4AAuthorizationHeaderFormat =
      "AWS4-ECDSA-P256-SHA256 Credential={}, SignedHeaders={}, Signature={}";
  static constexpr absl::string_view SigV4ACredentialScopeFormat = "{}/{}/aws4_request";
  static constexpr absl::string_view SigV4ASignatureVersion = "AWS4A";
  static constexpr absl::string_view SigV4AStringToSignFormat = "{}\n{}\n{}\n{}";
  static constexpr absl::string_view SigV4AAlgorithm = "AWS4-ECDSA-P256-SHA256";
};

enum SigV4AKeyDerivationResult {
  AkdrSuccess,
  AkdrNextCounter,
  AkdrFailure,
};

using AwsSigningHeaderExclusionVector = std::vector<envoy::type::matcher::v3::StringMatcher>;

/**
 * Implementation of the Signature V4A signing process.
 * See https://docs.aws.amazon.com/general/latest/gr/signature-version-4.html
 *
 * Query parameter support is implemented as per:
 * https://docs.aws.amazon.com/AmazonS3/latest/API/sigv4-query-string-auth.html
 */

class SigV4ASignerImpl : public SignerBaseImpl {

  // Allow friend access for signer corpus testing
  friend class SigV4ASignerImplFriend;

public:
  SigV4ASignerImpl(
      absl::string_view service_name, absl::string_view region,
      const CredentialsProviderSharedPtr& credentials_provider,
      Server::Configuration::CommonFactoryContext& context,
      const AwsSigningHeaderExclusionVector& matcher_config, const bool query_string = false,
      const uint16_t expiration_time = SignatureQueryParameterValues::DefaultExpiration)
      : SignerBaseImpl(service_name, region, credentials_provider, context, matcher_config,
                       query_string, expiration_time) {}

private:
  void addRegionHeader(Http::RequestHeaderMap& headers,
                       const absl::string_view override_region) const override;

  void addRegionQueryParam(Envoy::Http::Utility::QueryParamsMulti& query_params,
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

  absl::string_view getAlgorithmString() const override;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
