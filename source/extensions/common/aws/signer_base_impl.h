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

class SignatureConstantValues {
public:
  const std::string Aws4Request{"aws4_request"};
  const std::string HashedEmptyString{
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"};

  const std::string LongDateFormat{"%Y%m%dT%H%M00Z"};
  const std::string ShortDateFormat{"%Y%m%d"};
  const std::string UnsignedPayload{"UNSIGNED-PAYLOAD"};
};

using SignatureConstants = ConstSingleton<SignatureConstantValues>;

using AwsSigningHeaderExclusionVector = std::vector<envoy::type::matcher::v3::StringMatcher>;

/**
 * Implementation of the Signature V4 signing process.
 * See https://docs.aws.amazon.com/general/latest/gr/signature-version-4.html
 */
class SignerBaseImpl : public Signer, public Logger::Loggable<Logger::Id::aws> {
public:
  SignerBaseImpl(absl::string_view service_name, absl::string_view region,
                 const CredentialsProviderSharedPtr& credentials_provider, TimeSource& time_source,
                 const AwsSigningHeaderExclusionVector& matcher_config)
      : service_name_(service_name), region_(region), credentials_provider_(credentials_provider),
        time_source_(time_source), long_date_formatter_(SignatureConstants::get().LongDateFormat),
        short_date_formatter_(SignatureConstants::get().ShortDateFormat) {
    for (const auto& matcher : matcher_config) {
      excluded_header_matchers_.emplace_back(
          std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
              matcher));
    }
  }

  void sign(Http::RequestMessage& message, bool sign_body = false,
            const absl::string_view override_region = "") override;
  void sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
            const absl::string_view override_region = "") override;
  void signEmptyPayload(Http::RequestHeaderMap& headers,
                        const absl::string_view override_region = "") override;
  void signUnsignedPayload(Http::RequestHeaderMap& headers,
                           const absl::string_view override_region = "") override;

protected:
  std::string getRegion() const;

  std::string createContentHash(Http::RequestMessage& message, bool sign_body) const;

  virtual void addRegionHeader(Http::RequestHeaderMap& headers,
                               const absl::string_view override_region) const;

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

  std::vector<Matchers::StringMatcherPtr> defaultMatchers() const {
    std::vector<Matchers::StringMatcherPtr> matcher_ptrs{};
    for (const auto& header : default_excluded_headers_) {
      envoy::type::matcher::v3::StringMatcher m;
      m.set_exact(header);
      matcher_ptrs.emplace_back(
          std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
              m));
    }
    return matcher_ptrs;
  }

  const std::string service_name_;
  const std::string region_;
  const std::vector<std::string> default_excluded_headers_ = {
      Http::Headers::get().ForwardedFor.get(), Http::Headers::get().ForwardedProto.get(),
      "x-amzn-trace-id"};
  std::vector<Matchers::StringMatcherPtr> excluded_header_matchers_ = defaultMatchers();
  CredentialsProviderSharedPtr credentials_provider_;
  TimeSource& time_source_;
  DateFormatter long_date_formatter_;
  DateFormatter short_date_formatter_;
  const std::string blank_str_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
