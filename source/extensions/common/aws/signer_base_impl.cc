#include "source/extensions/common/aws/signer_base_impl.h"

#include <openssl/ssl.h>

#include <cstddef>
#include <cstdint>

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

void SignerBaseImpl::sign(Http::RequestMessage& message, bool sign_body,
                          const absl::string_view override_region) {

  const auto content_hash = createContentHash(message, sign_body);
  auto& headers = message.headers();
  sign(headers, content_hash, override_region);
}

void SignerBaseImpl::signEmptyPayload(Http::RequestHeaderMap& headers,
                                      const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256,
                       SignatureConstants::get().HashedEmptyString);
  sign(headers, SignatureConstants::get().HashedEmptyString, override_region);
}

void SignerBaseImpl::signUnsignedPayload(Http::RequestHeaderMap& headers,
                                         const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256,
                       SignatureConstants::get().UnsignedPayload);
  sign(headers, SignatureConstants::get().UnsignedPayload, override_region);
}

// Region support utilities for sigv4a
void SignerBaseImpl::addRegionHeader(
    ABSL_ATTRIBUTE_UNUSED Http::RequestHeaderMap& headers,
    ABSL_ATTRIBUTE_UNUSED const absl::string_view override_region) const {}
void SignerBaseImpl::addRegionQueryParam(
    ABSL_ATTRIBUTE_UNUSED Envoy::Http::Utility::QueryParamsMulti& query_params,
    ABSL_ATTRIBUTE_UNUSED const absl::string_view override_region) const {}

std::string SignerBaseImpl::getRegion() const { return region_; }

void SignerBaseImpl::sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
                          const absl::string_view override_region) {

  if (!query_string_) {
    headers.setReferenceKey(SignatureHeaders::get().ContentSha256, content_hash);
  }

  const auto& credentials = credentials_provider_->getCredentials();
  if (!credentials.accessKeyId() || !credentials.secretAccessKey()) {
    // Empty or "anonymous" credentials are a valid use-case for non-production environments.
    // This behavior matches what the AWS SDK would do.
    ENVOY_LOG_MISC(debug, "Sign exiting early - no credentials found");
    return;
  }

  if (headers.Method() == nullptr) {
    throw EnvoyException("Message is missing :method header");
  }
  if (headers.Path() == nullptr) {
    throw EnvoyException("Message is missing :path header");
  }

  const auto long_date = long_date_formatter_.now(time_source_);
  const auto short_date = short_date_formatter_.now(time_source_);

  if (!query_string_) {
    if (credentials.sessionToken()) {
      headers.addCopy(SignatureHeaders::get().SecurityToken, credentials.sessionToken().value());
    }
    headers.addCopy(SignatureHeaders::get().Date, long_date);
    addRegionHeader(headers, override_region);
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
    createQueryParams(
        query_params,
        createAuthorizationCredential(credentials.accessKeyId().value(), credential_scope),
        long_date, credentials.sessionToken(), canonical_headers, expiration_time_);

    headers.setPath(query_params.replaceQueryString(headers.Path()->value()));
  }

  const auto canonical_request = Utility::createCanonicalRequest(
      service_name_, headers.Method()->value().getStringView(),
      headers.Path()->value().getStringView(), canonical_headers, content_hash);
  ENVOY_LOG(debug, "Canonical request:\n{}", canonical_request);

  // Phase 2: Create a string to sign
  const auto string_to_sign = createStringToSign(canonical_request, long_date, credential_scope);
  ENVOY_LOG(debug, "String to sign:\n{}", string_to_sign);

  // Phase 3: Create a signature
  const auto signature =
      createSignature(credentials.accessKeyId().value(), credentials.secretAccessKey().value(),
                      short_date, string_to_sign, override_region);
  // Phase 4: Sign request
  if (query_string_) {
    // Append signature to existing query string
    query_params.add(SignatureQueryParameters::get().AmzSignature, signature);
    headers.setPath(query_params.replaceQueryString(headers.Path()->value()));
    ENVOY_LOG(debug, "Query string appended to path");
  } else {
    const auto authorization_header = createAuthorizationHeader(
        credentials.accessKeyId().value(), credential_scope, canonical_headers, signature);

    ENVOY_LOG(debug, "Signing request with: {}", authorization_header);
    headers.addCopy(Http::CustomHeaders::get().Authorization, authorization_header);
  }
}

std::string SignerBaseImpl::createContentHash(Http::RequestMessage& message, bool sign_body) const {
  if (!sign_body) {
    return SignatureConstants::get().HashedEmptyString;
  }
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto content_hash = message.body().length() > 0
                                ? Hex::encode(crypto_util.getSha256Digest(message.body()))
                                : SignatureConstants::get().HashedEmptyString;
  return content_hash;
}

std::string
SignerBaseImpl::createAuthorizationCredential(absl::string_view access_key_id,
                                              absl::string_view credential_scope) const {
  return fmt::format(SignatureConstants::get().AuthorizationCredentialFormat, access_key_id,
                     credential_scope);
}

void SignerBaseImpl::createQueryParams(Envoy::Http::Utility::QueryParamsMulti& query_params,
                                       const absl::string_view credential,
                                       const absl::string_view long_date,
                                       const absl::optional<std::string> session_token,
                                       const std::map<std::string, std::string>& signed_headers,
                                       const uint8_t expiration_time) const {
  // X-Amz-Algorithm
  query_params.add(SignatureQueryParameters::get().AmzAlgorithm, getAlgorithmString());
  // X-Amz-Date
  query_params.add(SignatureQueryParameters::get().AmzDate, long_date);
  // X-Amz-Expires
  query_params.add(SignatureQueryParameters::get().AmzExpires, std::to_string(expiration_time));

  // These three parameters can contain characters that require URL encoding
  if (session_token.has_value()) {
    // X-Amz-Security-Token
    query_params.add(SignatureQueryParameters::get().AmzSecurityToken,
                     Utility::encodeQueryParam(session_token.value()));
  }
  // X-Amz-Credential
  query_params.add(SignatureQueryParameters::get().AmzCredential,
                   Utility::encodeQueryParam(credential));
  // X-Amz-SignedHeaders
  query_params.add(SignatureQueryParameters::get().AmzSignedHeaders,
                   Utility::encodeQueryParam(Utility::joinCanonicalHeaderNames(signed_headers)));
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
