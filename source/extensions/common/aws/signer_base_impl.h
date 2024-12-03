#pragma once

#include <utility>

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/signer.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class SignatureHeaderValues {
public:
  const Http::LowerCaseString ContentSha256{"x-amz-content-sha256"};
  const Http::LowerCaseString Date{"x-amz-date"};
  const Http::LowerCaseString SecurityToken{"x-amz-security-token"};
};

using SignatureHeaders = ConstSingleton<SignatureHeaderValues>;

class SignatureQueryParameterValues {
public:
  // Query string parameters require camel case
  static constexpr absl::string_view AmzAlgorithm = "X-Amz-Algorithm";
  static constexpr absl::string_view AmzCredential = "X-Amz-Credential";
  static constexpr absl::string_view AmzDate = "X-Amz-Date";
  static constexpr absl::string_view AmzRegionSet = "X-Amz-Region-Set";
  static constexpr absl::string_view AmzSecurityToken = "X-Amz-Security-Token";
  static constexpr absl::string_view AmzSignature = "X-Amz-Signature";
  static constexpr absl::string_view AmzSignedHeaders = "X-Amz-SignedHeaders";
  static constexpr absl::string_view AmzExpires = "X-Amz-Expires";
  // Expiration time of query parameter request, in seconds
  static constexpr uint16_t DefaultExpiration = 5;
};

class SignatureConstants {
public:
  static constexpr absl::string_view Aws4Request = "aws4_request";
  static constexpr absl::string_view HashedEmptyString =
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

  static constexpr absl::string_view LongDateFormat = "%Y%m%dT%H%M%SZ";
  static constexpr absl::string_view ShortDateFormat = "%Y%m%d";
  static constexpr absl::string_view UnsignedPayload = "UNSIGNED-PAYLOAD";
  static constexpr absl::string_view AuthorizationCredentialFormat = "{}/{}";
};

using AwsSigningHeaderExclusionVector = std::vector<envoy::type::matcher::v3::StringMatcher>;

/**
 * Implementation of the Signature V4 signing process.
 * See https://docs.aws.amazon.com/general/latest/gr/signature-version-4.html
 *
 * Query parameter support is implemented as per:
 * https://docs.aws.amazon.com/AmazonS3/latest/API/sigv4-query-string-auth.html
 */
class SignerBaseImpl : public Signer, public Logger::Loggable<Logger::Id::aws> {
public:
  SignerBaseImpl(absl::string_view service_name, absl::string_view region,
                 const CredentialsProviderSharedPtr& credentials_provider,
                 Server::Configuration::CommonFactoryContext& context,
                 const AwsSigningHeaderExclusionVector& matcher_config,
                 const bool query_string = false,
                 const uint16_t expiration_time = SignatureQueryParameterValues::DefaultExpiration)
      : service_name_(service_name), region_(region),
        excluded_header_matchers_(defaultMatchers(context)),
        credentials_provider_(credentials_provider), query_string_(query_string),
        expiration_time_(expiration_time), time_source_(context.timeSource()),
        long_date_formatter_(std::string(SignatureConstants::LongDateFormat)),
        short_date_formatter_(std::string(SignatureConstants::ShortDateFormat)) {
    for (const auto& matcher : matcher_config) {
      excluded_header_matchers_.emplace_back(
          std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
              matcher, context));
    }
  }

  absl::Status sign(Http::RequestMessage& message, bool sign_body = false,
                    const absl::string_view override_region = "") override;
  absl::Status sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
                    const absl::string_view override_region = "") override;
  absl::Status signEmptyPayload(Http::RequestHeaderMap& headers,
                                const absl::string_view override_region = "") override;
  absl::Status signUnsignedPayload(Http::RequestHeaderMap& headers,
                                   const absl::string_view override_region = "") override;

protected:
  std::string getRegion() const;

  std::string createContentHash(Http::RequestMessage& message, bool sign_body) const;

  virtual void addRegionHeader(Http::RequestHeaderMap& headers,
                               const absl::string_view override_region) const;
  virtual void addRegionQueryParam(Envoy::Http::Utility::QueryParamsMulti& query_params,
                                   const absl::string_view override_region) const;

  virtual absl::string_view getAlgorithmString() const PURE;

  virtual std::string createCredentialScope(const absl::string_view short_date,
                                            const absl::string_view override_region) const PURE;

  virtual std::string createStringToSign(const absl::string_view canonical_request,
                                         const absl::string_view long_date,
                                         const absl::string_view credential_scope) const PURE;

  virtual std::string createSignature(const absl::string_view access_key_id,
                                      const absl::string_view secret_access_key,
                                      const absl::string_view short_date,
                                      const absl::string_view string_to_sign,
                                      const absl::string_view override_region) const PURE;

  virtual std::string
  createAuthorizationHeader(const absl::string_view access_key_id,
                            const absl::string_view credential_scope,
                            const std::map<std::string, std::string>& canonical_headers,
                            const absl::string_view signature) const PURE;

  std::string createAuthorizationCredential(absl::string_view access_key_id,
                                            absl::string_view credential_scope) const;

  void createQueryParams(Envoy::Http::Utility::QueryParamsMulti& query_params,
                         const absl::string_view authorization_credential,
                         const absl::string_view long_date,
                         const absl::optional<std::string> session_token,
                         const std::map<std::string, std::string>& signed_headers,
                         const uint16_t expiration_time) const;

  void addRequiredHeaders(Http::RequestHeaderMap& headers, const std::string long_date,
                          const absl::optional<std::string> session_token,
                          const absl::string_view override_region);

  std::vector<Matchers::StringMatcherPtr>
  defaultMatchers(Server::Configuration::CommonFactoryContext& context) const {
    std::vector<Matchers::StringMatcherPtr> matcher_ptrs{};
    for (const auto& header : default_excluded_headers_) {
      envoy::type::matcher::v3::StringMatcher m;
      m.set_exact(header);
      matcher_ptrs.emplace_back(
          std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
              m, context));
    }
    return matcher_ptrs;
  }

  const std::string service_name_;
  const std::string region_;
  const std::vector<std::string> default_excluded_headers_ = {
      Http::Headers::get().ForwardedFor.get(), Http::Headers::get().ForwardedProto.get(),
      "x-amzn-trace-id"};
  std::vector<Matchers::StringMatcherPtr> excluded_header_matchers_;
  CredentialsProviderSharedPtr credentials_provider_;
  const bool query_string_;
  const uint16_t expiration_time_;
  TimeSource& time_source_;
  DateFormatter long_date_formatter_;
  DateFormatter short_date_formatter_;
  const std::string blank_str_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
