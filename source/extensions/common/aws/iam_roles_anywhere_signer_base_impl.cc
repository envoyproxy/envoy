#include "source/extensions/common/aws/iam_roles_anywhere_signer_base_impl.h"

#include "envoy/http/query_params.h"

#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

absl::Status IAMRolesAnywhereSignerBaseImpl::sign(Http::RequestMessage& message, bool sign_body,
                                                  const absl::string_view override_region) {
  const auto content_hash = createContentHash(message, sign_body);
  auto& headers = message.headers();
  return sign(headers, content_hash, override_region);
}

absl::Status
IAMRolesAnywhereSignerBaseImpl::signEmptyPayload(Http::RequestHeaderMap& headers,
                                                 const absl::string_view override_region) {
  headers.setReference(IAMRolesAnywhereSignatureHeaders::get().ContentSha256,
                       IAMRolesAnywhereSignatureConstants::HashedEmptyString);
  return sign(headers, std::string(IAMRolesAnywhereSignatureConstants::HashedEmptyString),
              override_region);
}

absl::Status
IAMRolesAnywhereSignerBaseImpl::signUnsignedPayload(Http::RequestHeaderMap& headers,
                                                    const absl::string_view override_region) {
  headers.setReference(IAMRolesAnywhereSignatureHeaders::get().ContentSha256,
                       IAMRolesAnywhereSignatureConstants::UnsignedPayload);
  return sign(headers, std::string(IAMRolesAnywhereSignatureConstants::UnsignedPayload),
              override_region);
}

bool IAMRolesAnywhereSignerBaseImpl::addCallbackIfCredentialsPending(
    CredentialsPendingCallback&& cb) {
  return credentials_provider_chain_->addCallbackIfChainCredentialsPending(std::move(cb));
}

absl::Status IAMRolesAnywhereSignerBaseImpl::sign(Http::RequestHeaderMap& headers,
                                                  const std::string& content_hash,
                                                  const absl::string_view override_region) {

  const auto& x509_credentials = x509_credentials_provider_->getCredentials();

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
    headers.setReferenceKey(IAMRolesAnywhereSignatureHeaders::get().ContentSha256, content_hash);
  }

  addRequiredHeaders(headers, long_date);
  addRequiredCertHeaders(headers, x509_credentials);

  const auto canonical_headers =
      Utility::canonicalizeHeaders(headers, std::vector<Matchers::StringMatcherPtr>{},
                                   std::vector<Matchers::StringMatcherPtr>{});

  // Phase 1: Create a canonical request
  const auto credential_scope = createCredentialScope(short_date, override_region);

  // Handle query string parameters by appending them all to the path. Case is important for these
  // query parameters.
  auto query_params =
      Envoy::Http::Utility::QueryParamsMulti::parseQueryString(headers.getPathValue());

  auto canonical_request = Utility::createCanonicalRequest(
      headers.Method()->value().getStringView(), headers.Path()->value().getStringView(),
      canonical_headers,
      content_hash.empty() ? IAMRolesAnywhereSignatureConstants::HashedEmptyString : content_hash,
      Utility::shouldNormalizeUriPath(service_name_), Utility::useDoubleUriEncode(service_name_));
  ENVOY_LOG(debug, "Canonical request:\n{}", canonical_request);

  // Phase 2: Create a string to sign
  std::string string_to_sign =
      createStringToSign(x509_credentials, canonical_request, long_date, credential_scope);
  ENVOY_LOG(debug, "String to sign:\n{}", string_to_sign);

  // Phase 3: Create a signature
  auto signature = createSignature(x509_credentials, string_to_sign);
  if (!signature.ok()) {
    return absl::Status{absl::StatusCode::kInvalidArgument, signature.status().message()};
  }

  // Phase 4: Sign request

  std::string authorization_header = createAuthorizationHeader(
      x509_credentials, credential_scope, canonical_headers, signature.value());

  headers.setCopy(Http::CustomHeaders::get().Authorization, authorization_header);

  // Sanitize logged authorization header
  std::vector<std::string> sanitised_header =
      absl::StrSplit(authorization_header, absl::ByString("Signature="));
  ENVOY_LOG(debug, "Header signing - Authorization header (sanitised): {}Signature=*****",
            sanitised_header[0]);
  return absl::OkStatus();
}

void IAMRolesAnywhereSignerBaseImpl::addRequiredCertHeaders(
    Http::RequestHeaderMap& headers, const X509Credentials& x509_credentials) {
  headers.setCopy(IAMRolesAnywhereSignatureHeaders::get().X509,
                  x509_credentials.certificateDerB64().value());
  if (x509_credentials.certificateChainDerB64().has_value()) {
    headers.setCopy(IAMRolesAnywhereSignatureHeaders::get().X509Chain,
                    x509_credentials.certificateChainDerB64().value());
  }
}

void IAMRolesAnywhereSignerBaseImpl::addRequiredHeaders(Http::RequestHeaderMap& headers,
                                                        const std::string long_date) {
  // Explicitly remove Authorization and security token header if present
  headers.remove(Http::CustomHeaders::get().Authorization);

  headers.setCopy(IAMRolesAnywhereSignatureHeaders::get().Date, long_date);
  // addRegionHeader(headers, override_region);
}

std::string IAMRolesAnywhereSignerBaseImpl::createAuthorizationCredential(
    const X509Credentials& x509_credentials, absl::string_view credential_scope) const {
  return fmt::format(IAMRolesAnywhereSignatureConstants::AuthorizationCredentialFormat,
                     x509_credentials.certificateSerial().value(), credential_scope);
}

std::string IAMRolesAnywhereSignerBaseImpl::createContentHash(Http::RequestMessage& message,
                                                              bool sign_body) const {
  if (!sign_body) {
    return std::string(IAMRolesAnywhereSignatureConstants::HashedEmptyString);
  }
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto content_hash =
      message.body().length() > 0
          ? Hex::encode(crypto_util.getSha256Digest(message.body()))
          : std::string(IAMRolesAnywhereSignatureConstants::HashedEmptyString);
  return content_hash;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
