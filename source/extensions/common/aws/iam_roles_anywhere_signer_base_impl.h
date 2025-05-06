#pragma once
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/signer.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class IAMRolesAnywhereSignatureHeaderValues {
public:
  const Http::LowerCaseString ContentSha256{"x-amz-content-sha256"};
  const Http::LowerCaseString Date{"x-amz-date"};
  const Http::LowerCaseString X509{"x-amz-x509"};
  const Http::LowerCaseString X509Chain{"x-amz-x509-chain"};
};

using IAMRolesAnywhereSignatureHeaders = ConstSingleton<IAMRolesAnywhereSignatureHeaderValues>;

class IAMRolesAnywhereSignatureConstants {
public:
  static constexpr absl::string_view HashedEmptyString =
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

  static constexpr absl::string_view LongDateFormat = "%Y%m%dT%H%M%SZ";
  static constexpr absl::string_view ShortDateFormat = "%Y%m%d";
  static constexpr absl::string_view UnsignedPayload = "UNSIGNED-PAYLOAD";
  static constexpr absl::string_view AuthorizationCredentialFormat = "{}/{}";
  static constexpr uint16_t DefaultExpiration = 900;
};

using AwsSigningHeaderExclusionVector = std::vector<envoy::type::matcher::v3::StringMatcher>;

/**
 * Implementation of the Signature V4 signing process using X509 certificates.
 * See https://docs.aws.amazon.com/rolesanywhere/latest/userguide/authentication-sign-process.html
 */
class IAMRolesAnywhereSignerBaseImpl : public Signer, public Logger::Loggable<Logger::Id::aws> {
public:
  IAMRolesAnywhereSignerBaseImpl(absl::string_view service_name, absl::string_view region,
                                 const X509CredentialsProviderSharedPtr& credentials_provider,
                                 TimeSource& timesource)

      : service_name_(service_name), region_(region),
        x509_credentials_provider_(credentials_provider), time_source_(timesource),
        long_date_formatter_(std::string(IAMRolesAnywhereSignatureConstants::LongDateFormat)),
        short_date_formatter_(std::string(IAMRolesAnywhereSignatureConstants::ShortDateFormat)) {}

  absl::Status sign(Http::RequestMessage& message, bool sign_body = false,
                    const absl::string_view override_region = "") override;
  absl::Status signEmptyPayload(Http::RequestHeaderMap& headers,
                                const absl::string_view override_region = "") override;
  absl::Status signUnsignedPayload(Http::RequestHeaderMap& headers,
                                   const absl::string_view override_region = "") override;
  absl::Status sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
                    const absl::string_view override_region = "") override;
  bool addCallbackIfCredentialsPending(CredentialsPendingCallback&& cb) override;

protected:
  std::string createContentHash(Http::RequestMessage& message, bool sign_body) const;

  virtual std::string createCredentialScope(const absl::string_view short_date,
                                            const absl::string_view override_region) const PURE;

  virtual std::string createStringToSign(const X509Credentials x509_credentials,
                                         const absl::string_view canonical_request,
                                         const absl::string_view long_date,
                                         const absl::string_view credential_scope) const PURE;

  virtual std::string createSignature(const X509Credentials credentials,
                                      const absl::string_view string_to_sign) const PURE;

  virtual std::string
  createAuthorizationHeader(const X509Credentials x509_credentials,
                            const absl::string_view credential_scope,
                            const std::map<std::string, std::string>& canonical_headers,
                            const absl::string_view signature) const PURE;

  std::string createAuthorizationCredential(const X509Credentials x509_credentials,
                                            absl::string_view credential_scope) const;

  void addRequiredHeaders(Http::RequestHeaderMap& headers, const std::string long_date);

  void addRequiredCertHeaders(Http::RequestHeaderMap& headers, X509Credentials x509_credentials);

  const std::string service_name_;
  const std::string region_;
  CredentialsProviderChainSharedPtr credentials_provider_chain_;
  X509CredentialsProviderSharedPtr x509_credentials_provider_;
  TimeSource& time_source_;
  DateFormatter long_date_formatter_;
  DateFormatter short_date_formatter_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
