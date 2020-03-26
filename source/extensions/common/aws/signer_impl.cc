#include "extensions/common/aws/signer_impl.h"

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/crypto/utility.h"
#include "common/http/headers.h"

#include "extensions/common/aws/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

void SignerImpl::sign(Http::RequestMessage& message, bool sign_body) {
  const auto content_hash = createContentHash(message, sign_body);
  auto& headers = message.headers();
  sign(headers, content_hash);
}

void SignerImpl::sign(Http::RequestHeaderMap& headers) {
  // S3 payloads require special treatment.
  if (service_name_ == "s3") {
    headers.setReference(SignatureHeaders::get().ContentSha256,
                         SignatureConstants::get().UnsignedPayload);
    sign(headers, SignatureConstants::get().UnsignedPayload);
  } else {
    headers.setReference(SignatureHeaders::get().ContentSha256,
                         SignatureConstants::get().HashedEmptyString);
    sign(headers, SignatureConstants::get().HashedEmptyString);
  }
}

void SignerImpl::sign(Http::RequestHeaderMap& headers, const std::string& content_hash) {
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
  // Phase 1: Create a canonical request
  const auto canonical_headers = Utility::canonicalizeHeaders(headers);
  const auto canonical_request = Utility::createCanonicalRequest(
      method_header->value().getStringView(), path_header->value().getStringView(),
      canonical_headers, content_hash);
  ENVOY_LOG(debug, "Canonical request:\n{}", canonical_request);
  // Phase 2: Create a string to sign
  const auto credential_scope = createCredentialScope(short_date);
  const auto string_to_sign = createStringToSign(canonical_request, long_date, credential_scope);
  ENVOY_LOG(debug, "String to sign:\n{}", string_to_sign);
  // Phase 3: Create a signature
  const auto signature =
      createSignature(credentials.secretAccessKey().value(), short_date, string_to_sign);
  // Phase 4: Sign request
  const auto authorization_header = createAuthorizationHeader(
      credentials.accessKeyId().value(), credential_scope, canonical_headers, signature);
  ENVOY_LOG(debug, "Signing request with: {}", authorization_header);
  headers.addCopy(Http::Headers::get().Authorization, authorization_header);
}

std::string SignerImpl::createContentHash(Http::RequestMessage& message, bool sign_body) const {
  if (!sign_body) {
    return SignatureConstants::get().HashedEmptyString;
  }
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto content_hash = message.body()
                                ? Hex::encode(crypto_util.getSha256Digest(*message.body()))
                                : SignatureConstants::get().HashedEmptyString;
  return content_hash;
}

std::string SignerImpl::createCredentialScope(absl::string_view short_date) const {
  return fmt::format(SignatureConstants::get().CredentialScopeFormat, short_date, region_,
                     service_name_);
}

std::string SignerImpl::createStringToSign(absl::string_view canonical_request,
                                           absl::string_view long_date,
                                           absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  return fmt::format(
      SignatureConstants::get().StringToSignFormat, long_date, credential_scope,
      Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
}

std::string SignerImpl::createSignature(absl::string_view secret_access_key,
                                        absl::string_view short_date,
                                        absl::string_view string_to_sign) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto secret_key =
      absl::StrCat(SignatureConstants::get().SignatureVersion, secret_access_key);
  const auto date_key = crypto_util.getSha256Hmac(
      std::vector<uint8_t>(secret_key.begin(), secret_key.end()), short_date);
  const auto region_key = crypto_util.getSha256Hmac(date_key, region_);
  const auto service_key = crypto_util.getSha256Hmac(region_key, service_name_);
  const auto signing_key =
      crypto_util.getSha256Hmac(service_key, SignatureConstants::get().Aws4Request);
  return Hex::encode(crypto_util.getSha256Hmac(signing_key, string_to_sign));
}

std::string
SignerImpl::createAuthorizationHeader(absl::string_view access_key_id,
                                      absl::string_view credential_scope,
                                      const std::map<std::string, std::string>& canonical_headers,
                                      absl::string_view signature) const {
  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);
  return fmt::format(SignatureConstants::get().AuthorizationHeaderFormat, access_key_id,
                     credential_scope, signed_headers, signature);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
