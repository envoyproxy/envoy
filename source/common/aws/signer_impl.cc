#include "common/aws/signer_impl.h"

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/hex.h"
#include "common/common/stack_array.h"
#include "common/http/headers.h"

#include "openssl/evp.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"

namespace Envoy {
namespace Aws {
namespace Auth {

static const std::string AWS4{"AWS4"};
static const std::string AWS4_HMAC_SHA256{"AWS4-HMAC-SHA256"};
static const std::string AWS4_REQUEST{"aws4_request"};
static const std::string CREDENTIAL{"Credential"};
static const std::string SIGNED_HEADERS{"SignedHeaders"};
static const std::string SIGNATURE{"Signature"};
static const std::string HASHED_EMPTY_STRING{
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"};

DateFormatter SignerImpl::LONG_DATE_FORMATTER("%Y%m%dT%H%M00Z");
DateFormatter SignerImpl::SHORT_DATE_FORMATTER("%Y%m%d");
const Http::LowerCaseString SignerImpl::X_AMZ_SECURITY_TOKEN{"x-amz-security-token"};
const Http::LowerCaseString SignerImpl::X_AMZ_DATE{"x-amz-date"};
const Http::LowerCaseString SignerImpl::X_AMZ_CONTENT_SHA256{"x-amz-content-sha256"};

void SignerImpl::sign(Http::Message& message) const {
  const auto& credentials = credentials_provider_->getCredentials();
  if (!credentials.accessKeyId() || !credentials.secretAccessKey()) {
    return;
  }
  const auto& region = region_provider_->getRegion();
  if (!region) {
    throw EnvoyException("Could not determine AWS region");
  }
  auto& headers = message.headers();
  if (credentials.sessionToken()) {
    headers.addCopy(X_AMZ_SECURITY_TOKEN, credentials.sessionToken().value());
  }
  const auto long_date = LONG_DATE_FORMATTER.now(time_source_);
  const auto short_date = SHORT_DATE_FORMATTER.now(time_source_);
  headers.addCopy(X_AMZ_DATE, long_date);
  const auto content_hash = createContentHash(message);
  headers.addCopy(X_AMZ_CONTENT_SHA256, content_hash);
  // Phase 1: Create a canonical request
  const auto canonical_headers = canonicalizeHeaders(headers);
  const auto signing_headers = createSigningHeaders(canonical_headers);
  const auto canonical_request =
      createCanonicalRequest(message, canonical_headers, signing_headers, content_hash);
  ENVOY_LOG(debug, "Canonical request:\n{}", canonical_request);
  // Phase 2: Create a string to sign
  const auto credential_scope = createCredentialScope(short_date, region.value());
  const auto string_to_sign = createStringToSign(canonical_request, long_date, credential_scope);
  ENVOY_LOG(debug, "String to sign:\n{}", string_to_sign);
  // Phase 3: Create a signature
  const auto signature = createSignature(credentials.secretAccessKey().value(), short_date,
                                         region.value(), string_to_sign);
  // Phase 4: Sign request
  const auto authorization_header = createAuthorizationHeader(
      credentials.accessKeyId().value(), credential_scope, signing_headers, signature);
  ENVOY_LOG(debug, "Signing request with: {}", authorization_header);
  headers.addCopy(Http::Headers::get().Authorization, authorization_header);
}

std::string SignerImpl::createContentHash(Http::Message& message) const {
  if (!message.body()) {
    return HASHED_EMPTY_STRING;
  }
  return Hex::encode(hash(*message.body()));
}

std::string SignerImpl::createCanonicalRequest(
    Http::Message& message, const std::map<std::string, std::string>& canonical_headers,
    const std::string& signing_headers, const std::string& content_hash) const {
  const auto& headers = message.headers();
  std::stringstream out;
  // Http method
  const auto* method_header = headers.Method();
  if (method_header == nullptr || method_header->value().empty()) {
    throw EnvoyException("Message is missing :method header");
  }
  out << method_header->value().c_str() << "\n";
  // Path
  const auto* path_header = headers.Path();
  if (path_header == nullptr || path_header->value().empty()) {
    throw EnvoyException("Message is missing :path header");
  }
  const auto& path_value = path_header->value();
  const auto path = StringUtil::cropRight(path_value.getStringView(), "?");
  if (path.empty()) {
    out << "/";
  } else {
    out << path;
  }
  out << "\n";
  // Query string
  const auto query = StringUtil::cropLeft(path_value.getStringView(), "?");
  if (query != path) {
    out << query;
  }
  out << "\n";
  // Headers
  for (const auto& header : canonical_headers) {
    out << header.first << ":" << header.second << "\n";
  }
  out << "\n" << signing_headers << "\n";
  // Content Hash
  out << content_hash;
  return out.str();
}

std::string SignerImpl::createSigningHeaders(
    const std::map<std::string, std::string>& canonical_headers) const {
  std::vector<std::string> keys;
  keys.reserve(canonical_headers.size());
  for (const auto& header : canonical_headers) {
    keys.emplace_back(header.first);
  }
  return StringUtil::join(keys, ";");
}

std::string SignerImpl::createCredentialScope(const std::string& short_date,
                                              const std::string& region) const {
  std::stringstream out;
  out << short_date << "/" << region << "/" << service_name_ << "/" << AWS4_REQUEST;
  return out.str();
}

std::string SignerImpl::createStringToSign(const std::string& canonical_request,
                                           const std::string& long_date,
                                           const std::string& credential_scope) const {
  std::stringstream out;
  out << AWS4_HMAC_SHA256 << "\n";
  out << long_date << "\n";
  out << credential_scope << "\n";
  out << Hex::encode(hash(Buffer::OwnedImpl(canonical_request)));
  return out.str();
}

std::string SignerImpl::createSignature(const std::string& secret_access_key,
                                        const std::string& short_date, const std::string& region,
                                        const std::string& string_to_sign) const {
  const auto k_secret = AWS4 + secret_access_key;
  const auto k_date = hmac(std::vector<uint8_t>(k_secret.begin(), k_secret.end()), short_date);
  const auto k_region = hmac(k_date, region);
  const auto k_service = hmac(k_region, service_name_);
  const auto k_signing = hmac(k_service, AWS4_REQUEST);
  return Hex::encode(hmac(k_signing, string_to_sign));
}

std::string SignerImpl::createAuthorizationHeader(const std::string& access_key_id,
                                                  const std::string& credential_scope,
                                                  const std::string& signing_headers,
                                                  const std::string& signature) const {
  std::stringstream out;
  out << AWS4_HMAC_SHA256 << " ";
  out << CREDENTIAL << "=" << access_key_id << "/" << credential_scope << ", ";
  out << SIGNED_HEADERS << "=" << signing_headers << ", ";
  out << SIGNATURE << "=" << signature;
  return out.str();
}

std::map<std::string, std::string>
SignerImpl::canonicalizeHeaders(const Http::HeaderMap& headers) const {
  std::map<std::string, std::string> out;
  headers.iterate(
      [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
        auto* map = static_cast<std::map<std::string, std::string>*>(context);
        const auto& key = entry.key().getStringView();
        // Pseudo-headers should not be canonicalized
        if (key.empty() || key[0] == ':') {
          return Http::HeaderMap::Iterate::Continue;
        }
        // Join multi-line headers with commas
        std::vector<std::string> lines;
        for (const auto& line : StringUtil::splitToken(entry.value().getStringView(), "\n")) {
          lines.emplace_back(StringUtil::trim(line));
        }
        auto value = StringUtil::join(lines, ",");
        // Remove duplicate spaces
        const auto end = std::unique(value.begin(), value.end(), [](char lhs, char rhs) {
          return (lhs == rhs) && (lhs == ' ');
        });
        value.erase(end, value.end());
        map->emplace(entry.key().c_str(), value);
        return Http::HeaderMap::Iterate::Continue;
      },
      &out);
  // The AWS SDK has a quirk where it removes "default ports" (80, 443) from the host headers
  // Additionally, we canonicalize the :authority header as "host"
  const auto* authority_header = headers.Host();
  if (authority_header != nullptr && !authority_header->value().empty()) {
    const auto& value = authority_header->value().getStringView();
    const auto parts = StringUtil::splitToken(value, ":");
    if (parts.size() > 1 && (parts[1] == "80" || parts[1] == "443")) {
      out.emplace(Http::Headers::get().HostLegacy.get(),
                  std::string(parts[0].data(), parts[0].size()));
    } else {
      out.emplace(Http::Headers::get().HostLegacy.get(), std::string(value.data(), value.size()));
    }
  }
  return out;
}

std::vector<uint8_t> SignerImpl::hash(const Buffer::Instance& buffer) const {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  EVP_MD_CTX ctx;
  auto code = EVP_DigestInit(&ctx, EVP_sha256());
  RELEASE_ASSERT(code == 1, "Failed to init digest context");
  const auto num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  for (const auto& slice : slices) {
    code = EVP_DigestUpdate(&ctx, slice.mem_, slice.len_);
    RELEASE_ASSERT(code == 1, "Failed to update digest");
  }
  unsigned int digest_length;
  code = EVP_DigestFinal(&ctx, digest.data(), &digest_length);
  RELEASE_ASSERT(code == 1, "Failed to finalize digest");
  RELEASE_ASSERT(digest_length == SHA256_DIGEST_LENGTH, "Digest length mismatch");
  return digest;
}

std::vector<uint8_t> SignerImpl::hmac(const std::vector<uint8_t>& key,
                                      const std::string& string) const {
  std::vector<uint8_t> mac(EVP_MAX_MD_SIZE);
  HMAC_CTX ctx;
  RELEASE_ASSERT(key.size() < std::numeric_limits<int>::max(), "Hmac key is too long");
  HMAC_CTX_init(&ctx);
  auto code = HMAC_Init_ex(&ctx, key.data(), static_cast<int>(key.size()), EVP_sha256(), nullptr);
  RELEASE_ASSERT(code == 1, "Failed to init hmac context");
  code = HMAC_Update(&ctx, reinterpret_cast<const uint8_t*>(string.data()), string.size());
  RELEASE_ASSERT(code == 1, "Failed to update hmac");
  unsigned int len;
  code = HMAC_Final(&ctx, mac.data(), &len);
  RELEASE_ASSERT(code == 1, "Failed to finalize hmac");
  RELEASE_ASSERT(len <= EVP_MAX_MD_SIZE, "Hmac length too large");
  HMAC_CTX_cleanup(&ctx);
  mac.resize(len);
  return mac;
}

} // namespace Auth
} // namespace Aws
} // namespace Envoy
