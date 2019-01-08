#pragma once

#include "common/aws/credentials_provider.h"
#include "common/aws/region_provider.h"
#include "common/aws/signer.h"
#include "common/common/logger.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Aws {
namespace Auth {

/**
 * Implementation of the Signature V4 signing process.
 * See https://docs.aws.amazon.com/general/latest/gr/signature-version-4.html
 */
class SignerImpl : public Signer, public Logger::Loggable<Logger::Id::aws> {
public:
  SignerImpl(const std::string& service_name,
             const CredentialsProviderSharedPtr& credentials_provider,
             const RegionProviderSharedPtr& region_provider, TimeSource& time_source)
      : service_name_(service_name), credentials_provider_(credentials_provider),
        region_provider_(region_provider), time_source_(time_source) {}

  void sign(Http::Message& message) const override;

  static const Http::LowerCaseString X_AMZ_SECURITY_TOKEN;
  static const Http::LowerCaseString X_AMZ_DATE;
  static const Http::LowerCaseString X_AMZ_CONTENT_SHA256;

private:
  friend class SignerImplTest;

  static DateFormatter LONG_DATE_FORMATTER;
  static DateFormatter SHORT_DATE_FORMATTER;
  const std::string service_name_;
  CredentialsProviderSharedPtr credentials_provider_;
  RegionProviderSharedPtr region_provider_;
  TimeSource& time_source_;

  std::string createContentHash(Http::Message& message) const;

  std::string createCanonicalRequest(Http::Message& message,
                                     const std::map<std::string, std::string>& canonical_headers,
                                     const std::string& signing_headers,
                                     const std::string& content_hash) const;

  std::string
  createSigningHeaders(const std::map<std::string, std::string>& canonical_headers) const;

  std::string createCredentialScope(const std::string& short_date, const std::string& region) const;

  std::string createStringToSign(const std::string& canonical_request, const std::string& long_date,
                                 const std::string& credential_scope) const;

  std::string createSignature(const std::string& secret_access_key, const std::string& short_date,
                              const std::string& region, const std::string& string_to_sign) const;

  std::string createAuthorizationHeader(const std::string& access_key_id,
                                        const std::string& credential_scope,
                                        const std::string& signing_headers,
                                        const std::string& signature) const;

  std::map<std::string, std::string> canonicalizeHeaders(const Http::HeaderMap& headers) const;

  std::vector<uint8_t> hash(const Buffer::Instance& buffer) const;

  std::vector<uint8_t> hmac(const std::vector<uint8_t>& key, const std::string& string) const;
};

} // namespace Auth
} // namespace Aws
} // namespace Envoy
