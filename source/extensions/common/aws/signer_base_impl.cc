#include "source/extensions/common/aws/signer_base_impl.h"

#include <openssl/ssl.h>

#include <cstddef>
#include <cstdint>
#include <regex>

#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/common/aws/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

absl::Status SignerBaseImpl::sign(Http::RequestMessage& message, bool sign_body,
                                  const absl::string_view override_region) {

  const auto content_hash = createContentHash(message, sign_body);
  auto& headers = message.headers();
  return sign(headers, content_hash, override_region);
}

absl::Status SignerBaseImpl::signEmptyPayload(Http::RequestHeaderMap& headers,
                                              const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256,
                       SignatureConstants::HashedEmptyString);
  return sign(headers, std::string(SignatureConstants::HashedEmptyString), override_region);
}

absl::Status SignerBaseImpl::signUnsignedPayload(Http::RequestHeaderMap& headers,
                                                 const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256, SignatureConstants::UnsignedPayload);
  return sign(headers, std::string(SignatureConstants::UnsignedPayload), override_region);
}

// Region support utilities for sigv4a
void SignerBaseImpl::addRegionHeader(
    ABSL_ATTRIBUTE_UNUSED Http::RequestHeaderMap& headers,
    ABSL_ATTRIBUTE_UNUSED const absl::string_view override_region) const {}
void SignerBaseImpl::addRegionQueryParam(
    ABSL_ATTRIBUTE_UNUSED Envoy::Http::Utility::QueryParamsMulti& query_params,
    ABSL_ATTRIBUTE_UNUSED const absl::string_view override_region) const {}

std::string SignerBaseImpl::getRegion() const { return region_; }

absl::Status SignerBaseImpl::sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
                                  const absl::string_view override_region) {

  ASSERT(credentials_provider_ != nullptr);
  ASSERT(x509_credentials_provider_ == nullptr);

  if (!query_string_ && !content_hash.empty()) {
    headers.setReferenceKey(SignatureHeaders::get().ContentSha256, content_hash);
  }

  const auto& credentials = credentials_provider_->getCredentials();
  if (!credentials.accessKeyId() || !credentials.secretAccessKey()) {
    // Empty or "anonymous" credentials are a valid use-case for non-production environments.
    // This behavior matches what the AWS SDK would do.
    ENVOY_LOG_MISC(debug, "Sign exiting early - no credentials found");
    return absl::OkStatus();
  }

  if (headers.Method() == nullptr) {
    return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :method header"};
  }
  if (headers.Path() == nullptr) {
    return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :path header"};
  }

  const auto long_date = long_date_formatter_.now(time_source_);
  const auto short_date = short_date_formatter_.now(time_source_);

  if (!query_string_) {
    addRequiredHeaders(headers, long_date, credentials.sessionToken(), override_region);
  }

  const auto canonical_headers = Utility::canonicalizeHeaders(headers, excluded_header_matchers_);

  // Phase 1: Create a canonical request
  const auto credential_scope = createCredentialScope(short_date, override_region);

  // Handle query string parameters by appending them all to the path. Case is important for these
  // query parameters.
  auto query_params =
      Envoy::Http::Utility::QueryParamsMulti::parseQueryString(headers.getPathValue());
  if (query_string_) {
    addRegionQueryParam(query_params, override_region);
    createQueryParams(query_params, createAuthorizationCredential(credentials, credential_scope),
                      long_date, credentials.sessionToken(), canonical_headers, expiration_time_);

    headers.setPath(query_params.replaceQueryString(headers.Path()->value()));
  }

  auto canonical_request = Utility::createCanonicalRequest(
      headers.Method()->value().getStringView(), headers.Path()->value().getStringView(),
      canonical_headers,
      content_hash.empty() ? SignatureConstants::HashedEmptyString : content_hash,
      Utility::shouldNormalizeUriPath(service_name_), Utility::useDoubleUriEncode(service_name_));
  ENVOY_LOG(debug, "Canonical request:\n{}", canonical_request);

  // Phase 2: Create a string to sign
  const auto string_to_sign = createStringToSign(canonical_request, long_date, credential_scope);
  ENVOY_LOG(debug, "String to sign:\n{}", string_to_sign);

  // Phase 3: Create a signature
  const auto signature = createSignature(credentials, short_date, string_to_sign, override_region);
  // Phase 4: Sign request
  if (query_string_) {
    // Append signature to existing query string
    query_params.add(SignatureQueryParameterValues::AmzSignature, signature);
    headers.setPath(query_params.replaceQueryString(Http::HeaderString(headers.getPathValue())));
    // Sanitize logged query string
    query_params.overwrite(SignatureQueryParameterValues::AmzSignature, "*****");
    if (query_params.getFirstValue(SignatureQueryParameterValues::AmzSecurityToken)) {
      query_params.overwrite(SignatureQueryParameterValues::AmzSecurityToken, "*****");
    }
    auto sanitised_query_string =
        query_params.replaceQueryString(Http::HeaderString(headers.getPathValue()));
    ENVOY_LOG(debug, "Query string signing - New path (sanitised): {}", sanitised_query_string);

  } else {
    const auto authorization_header =
        createAuthorizationHeader(credentials, credential_scope, canonical_headers, signature);

    headers.setCopy(Http::CustomHeaders::get().Authorization, authorization_header);

    // Sanitize logged authorization header
    std::vector<std::string> sanitised_header =
        absl::StrSplit(authorization_header, absl::ByString("Signature="));
    ENVOY_LOG(debug, "Header signing - Authorization header (sanitised): {}Signature=*****",
              sanitised_header[0]);
  }
  return absl::OkStatus();
}

void SignerBaseImpl::addRequiredHeaders(Http::RequestHeaderMap& headers,
                                        const std::string long_date,
                                        const absl::optional<std::string> session_token,
                                        const absl::string_view override_region) {
  // Explicitly remove Authorization and security token header if present
  headers.remove(Http::CustomHeaders::get().Authorization);
  headers.remove(SignatureHeaders::get().SecurityToken);

  if (session_token.has_value() && !session_token.value().empty()) {
    headers.setCopy(SignatureHeaders::get().SecurityToken, session_token.value());
  }

  headers.setCopy(SignatureHeaders::get().Date, long_date);
  addRegionHeader(headers, override_region);
}

std::string SignerBaseImpl::createContentHash(Http::RequestMessage& message, bool sign_body) const {
  if (!sign_body) {
    return std::string(SignatureConstants::HashedEmptyString);
  }
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto content_hash = message.body().length() > 0
                                ? Hex::encode(crypto_util.getSha256Digest(message.body()))
                                : std::string(SignatureConstants::HashedEmptyString);
  return content_hash;
}

std::string
SignerBaseImpl::createAuthorizationCredential(const Credentials credentials,
                                              absl::string_view credential_scope) const {
  return fmt::format(SignatureConstants::AuthorizationCredentialFormat,
                     credentials.accessKeyId().value(), credential_scope);
}

std::string
SignerBaseImpl::createAuthorizationCredential(const X509Credentials x509_credentials,
                                              absl::string_view credential_scope) const {
  return fmt::format(SignatureConstants::AuthorizationCredentialFormat,
                     x509_credentials.certificateSerial().value(), credential_scope);
}

void SignerBaseImpl::createQueryParams(Envoy::Http::Utility::QueryParamsMulti& query_params,
                                       const absl::string_view credential,
                                       const absl::string_view long_date,
                                       const absl::optional<std::string> session_token,
                                       const std::map<std::string, std::string>& signed_headers,
                                       const uint16_t expiration_time) const {
  // X-Amz-Algorithm
  query_params.add(SignatureQueryParameterValues::AmzAlgorithm, getAlgorithmString());
  // X-Amz-Date
  query_params.add(SignatureQueryParameterValues::AmzDate, long_date);
  // X-Amz-Expires
  query_params.add(SignatureQueryParameterValues::AmzExpires, std::to_string(expiration_time));

  // These three parameters can contain characters that require URL encoding
  if (session_token.has_value()) {
    // X-Amz-Security-Token
    query_params.add(
        SignatureQueryParameterValues::AmzSecurityToken,
        Envoy::Http::Utility::PercentEncoding::urlEncodeQueryParameter(session_token.value()));
  }
  // X-Amz-Credential
  query_params.add(SignatureQueryParameterValues::AmzCredential,
                   Utility::encodeQueryComponent(credential));
  // X-Amz-SignedHeaders
  query_params.add(
      SignatureQueryParameterValues::AmzSignedHeaders,
      Utility::encodeQueryComponent(Utility::joinCanonicalHeaderNames(signed_headers)));
}

absl::Status SignerBaseImpl::signX509(Http::RequestMessage& message, bool sign_body,
                                      const absl::string_view override_region) {
  const auto content_hash = createContentHash(message, sign_body);
  auto& headers = message.headers();
  return signX509(headers, content_hash, override_region);
}

absl::Status SignerBaseImpl::signX509EmptyPayload(Http::RequestHeaderMap& headers,
                                                  const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256,
                       SignatureConstants::HashedEmptyString);
  return signX509(headers, std::string(SignatureConstants::HashedEmptyString), override_region);
}

absl::Status SignerBaseImpl::signX509UnsignedPayload(Http::RequestHeaderMap& headers,
                                                     const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256, SignatureConstants::UnsignedPayload);
  return signX509(headers, std::string(SignatureConstants::UnsignedPayload), override_region);
}

absl::Status SignerBaseImpl::signX509(Http::RequestHeaderMap& headers,
                                      const std::string& content_hash,
                                      const absl::string_view override_region) {

  const auto& x509_credentials = x509_credentials_provider_->getCredentials();

  ASSERT(credentials_provider_ == nullptr);
  ASSERT(x509_credentials_provider_ != nullptr);

  if (!x509_credentials.certificateDerB64().has_value() ||
      !x509_credentials.certificatePrivateKey().has_value() ||
      !x509_credentials.publicKeySignatureAlgorithm().has_value()) {
    return absl::Status{absl::StatusCode::kInvalidArgument,
                        "Unable to sign IAM Roles Anywhere payload - no x509 credentials found"};
  }

  if (headers.Method() == nullptr) {
    return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :method header"};
  }
  if (headers.Path() == nullptr) {
    return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :path header"};
  }

  ENVOY_LOG(debug, "Begin IAM Roles Anywhere signing");

  const auto long_date = long_date_formatter_.now(time_source_);
  const auto short_date = short_date_formatter_.now(time_source_);

  if (!content_hash.empty()) {
    headers.setReferenceKey(SignatureHeaders::get().ContentSha256, content_hash);
  }

  addRequiredHeaders(headers, long_date, absl::nullopt, override_region);
  addRequiredCertHeaders(headers, x509_credentials);

  const auto canonical_headers = Utility::canonicalizeHeaders(headers, excluded_header_matchers_);

  // Phase 1: Create a canonical request
  const auto credential_scope = createCredentialScope(short_date, override_region);

  // Handle query string parameters by appending them all to the path. Case is important for these
  // query parameters.
  auto query_params =
      Envoy::Http::Utility::QueryParamsMulti::parseQueryString(headers.getPathValue());

  auto canonical_request = Utility::createCanonicalRequest(
      headers.Method()->value().getStringView(), headers.Path()->value().getStringView(),
      canonical_headers,
      content_hash.empty() ? SignatureConstants::HashedEmptyString : content_hash,
      Utility::shouldNormalizeUriPath(service_name_), Utility::useDoubleUriEncode(service_name_));
  ENVOY_LOG(debug, "Canonical request:\n{}", canonical_request);

  // Phase 2: Create a string to sign
  std::string string_to_sign =
      createStringToSign(x509_credentials, canonical_request, long_date, credential_scope);
  ENVOY_LOG(debug, "String to sign:\n{}", string_to_sign);

  // Phase 3: Create a signature
  std::string signature = createSignature(x509_credentials, string_to_sign);
  // Phase 4: Sign request

  std::string authorization_header =
      createAuthorizationHeader(x509_credentials, credential_scope, canonical_headers, signature);

  headers.setCopy(Http::CustomHeaders::get().Authorization, authorization_header);

  // Sanitize logged authorization header
  std::vector<std::string> sanitised_header =
      absl::StrSplit(authorization_header, absl::ByString("Signature="));
  ENVOY_LOG(debug, "Header signing - Authorization header (sanitised): {}Signature=*****",
            sanitised_header[0]);
  return absl::OkStatus();
}

void SignerBaseImpl::addRequiredCertHeaders(Http::RequestHeaderMap& headers,
                                            X509Credentials x509_credentials) {
  headers.setCopy(SignatureHeaders::get().X509, x509_credentials.certificateDerB64().value());
  if (x509_credentials.certificateChainDerB64().has_value()) {
    headers.setCopy(SignatureHeaders::get().X509Chain,
                    x509_credentials.certificateChainDerB64().value());
  }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
