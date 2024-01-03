#include "source/extensions/common/aws/signer_base_impl.h"

#include <openssl/ssl.h>

#include <cstddef>

#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
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

// Required only for sigv4a
void SignerBaseImpl::addRegionHeader(
    ABSL_ATTRIBUTE_UNUSED Http::RequestHeaderMap& headers,
    ABSL_ATTRIBUTE_UNUSED const absl::string_view override_region) const {}

std::string SignerBaseImpl::getRegion() const { return region_; }

void SignerBaseImpl::sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
                          const absl::string_view override_region) {

  headers.setReferenceKey(SignatureHeaders::get().ContentSha256, content_hash);
  const auto& credentials = credentials_provider_->getCredentials();
  if (!credentials.accessKeyId() || !credentials.secretAccessKey()) {
    // Empty or "anonymous" credentials are a valid use-case for non-production environments.
    // This behavior matches what the AWS SDK would do.
    return;
  }
  const auto* method_header = headers.Method();
  if (method_header == nullptr) {
    throw EnvoyException("Message is missing :method header");
  }
  const auto* path_header = headers.Path();
  if (path_header == nullptr) {
    throw EnvoyException("Message is missing :path header");
  }

  if (credentials.sessionToken()) {
    headers.addCopy(SignatureHeaders::get().SecurityToken, credentials.sessionToken().value());
  }
  const auto long_date = long_date_formatter_.now(time_source_);
  const auto short_date = short_date_formatter_.now(time_source_);
  headers.addCopy(SignatureHeaders::get().Date, long_date);

  addRegionHeader(headers, override_region);

  // Phase 1: Create a canonical request
  const auto canonical_headers = Utility::canonicalizeHeaders(headers, excluded_header_matchers_);
  const auto canonical_request = Utility::createCanonicalRequest(
      service_name_, method_header->value().getStringView(), path_header->value().getStringView(),
      canonical_headers, content_hash);
  ENVOY_LOG(debug, "Canonical request:\n{}", canonical_request);

  // Phase 2: Create a string to sign
  const auto credential_scope = createCredentialScope(short_date, override_region);
  const auto string_to_sign = createStringToSign(canonical_request, long_date, credential_scope);
  ENVOY_LOG(debug, "String to sign:\n{}", string_to_sign);

  // Phase 3: Create a signature
  const auto signature =
      createSignature(credentials.accessKeyId().value(), credentials.secretAccessKey().value(),
                      short_date, string_to_sign, override_region);
  // Phase 4: Sign request
  const auto authorization_header = createAuthorizationHeader(
      credentials.accessKeyId().value(), credential_scope, canonical_headers, signature);
  ENVOY_LOG(debug, "Signing request with: {}", authorization_header);
  headers.addCopy(Http::CustomHeaders::get().Authorization, authorization_header);
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

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
