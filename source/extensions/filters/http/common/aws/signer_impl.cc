#include "extensions/filters/http/common/aws/signer_impl.h"

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/http/headers.h"
#include "common/ssl/utility.h"

#include "extensions/filters/http/common/aws/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

void SignerImpl::sign(Http::Message& message) {
  const auto& credentials = credentials_provider_->getCredentials();
  if (!credentials.accessKeyId() || !credentials.secretAccessKey()) {
    // Empty or "anonymous" credentials are a valid use-case for non-production environments.
    // This behavior matches what the AWS SDK would do.
    return;
  }
  auto& headers = message.headers();
  if (credentials.sessionToken()) {
    headers.addCopy(SignatureHeaders::get().SecurityToken, credentials.sessionToken().value());
  }
  const auto long_date = long_date_formatter_.now(time_source_);
  const auto short_date = short_date_formatter_.now(time_source_);
  headers.addCopy(SignatureHeaders::get().Date, long_date);
  const auto content_hash = createContentHash(message);
  headers.addCopy(SignatureHeaders::get().ContentSha256, content_hash);
  // Phase 1: Create a canonical request
  const auto canonical_headers = Utility::canonicalizeHeaders(headers);
  const auto signing_headers = createSigningHeaders(canonical_headers);
  const auto canonical_request =
      createCanonicalRequest(message, canonical_headers, signing_headers, content_hash);
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
      credentials.accessKeyId().value(), credential_scope, signing_headers, signature);
  ENVOY_LOG(debug, "Signing request with: {}", authorization_header);
  headers.addCopy(Http::Headers::get().Authorization, authorization_header);
}

std::string SignerImpl::createContentHash(Http::Message& message) const {
  if (!message.body()) {
    return SignatureConstants::get().HashedEmptyString;
  }
  return Hex::encode(Ssl::Utility::getSha256Digest(*message.body()));
}

std::string SignerImpl::createCanonicalRequest(
    Http::Message& message, const std::map<std::string, std::string>& canonical_headers,
    absl::string_view signing_headers, absl::string_view content_hash) const {
  std::vector<absl::string_view> parts;
  const auto* method_header = message.headers().Method();
  if (method_header == nullptr || method_header->value().empty()) {
    throw EnvoyException("Message is missing :method header");
  }
  parts.emplace_back(method_header->value().getStringView());
  const auto* path_header = message.headers().Path();
  if (path_header == nullptr || path_header->value().empty()) {
    throw EnvoyException("Message is missing :path header");
  }
  // don't include the query part of the path
  const auto path = StringUtil::cropRight(path_header->value().getStringView(), "?");
  parts.emplace_back(path.empty() ? "/" : path);
  const auto query = StringUtil::cropLeft(path_header->value().getStringView(), "?");
  // if query == path, then there is no query
  parts.emplace_back(query == path ? "" : query);
  std::vector<std::string> formatted_headers;
  formatted_headers.reserve(canonical_headers.size());
  for (const auto& header : canonical_headers) {
    formatted_headers.emplace_back(fmt::format("{}:{}", header.first, header.second));
    parts.emplace_back(formatted_headers.back());
  }
  // need an extra blank space after the canonical headers
  parts.emplace_back("");
  parts.emplace_back(signing_headers);
  parts.emplace_back(content_hash);
  return absl::StrJoin(parts, "\n");
}

std::string SignerImpl::createSigningHeaders(
    const std::map<std::string, std::string>& canonical_headers) const {
  std::vector<absl::string_view> keys;
  keys.reserve(canonical_headers.size());
  for (const auto& header : canonical_headers) {
    keys.emplace_back(header.first);
  }
  return absl::StrJoin(keys, ";");
}

std::string SignerImpl::createCredentialScope(absl::string_view short_date) const {
  return fmt::format(SignatureConstants::get().CredentialScopeFormat, short_date, region_,
                     service_name_);
}

std::string SignerImpl::createStringToSign(absl::string_view canonical_request,
                                           absl::string_view long_date,
                                           absl::string_view credential_scope) const {
  return fmt::format(
      SignatureConstants::get().StringToSignFormat, long_date, credential_scope,
      Hex::encode(Ssl::Utility::getSha256Digest(Buffer::OwnedImpl(canonical_request))));
}

std::string SignerImpl::createSignature(absl::string_view secret_access_key,
                                        absl::string_view short_date,
                                        absl::string_view string_to_sign) const {
  const auto secret_key =
      absl::StrCat(SignatureConstants::get().SignatureVersion, secret_access_key);
  const auto date_key = Ssl::Utility::getSha256Hmac(
      std::vector<uint8_t>(secret_key.begin(), secret_key.end()), short_date);
  const auto region_key = Ssl::Utility::getSha256Hmac(date_key, region_);
  const auto service_key = Ssl::Utility::getSha256Hmac(region_key, service_name_);
  const auto signing_key =
      Ssl::Utility::getSha256Hmac(service_key, SignatureConstants::get().Aws4Request);
  return Hex::encode(Ssl::Utility::getSha256Hmac(signing_key, string_to_sign));
}

std::string SignerImpl::createAuthorizationHeader(absl::string_view access_key_id,
                                                  absl::string_view credential_scope,
                                                  absl::string_view signing_headers,
                                                  absl::string_view signature) const {
  return fmt::format(SignatureConstants::get().AuthorizationHeaderFormat, access_key_id,
                     credential_scope, signing_headers, signature);
}

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy