#include "source/extensions/common/aws/signer_impl.h"

#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/extensions/common/aws/utility.h"

#include "absl/strings/str_join.h"
#include <cstddef>

#include <openssl/ssl.h>

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

// TODO: (nbaws) update to SigV4 naming along with other referring plugins
void SignerImpl::sign(Http::RequestMessage& message, bool sign_body,
                      const absl::string_view override_region) {
  const auto content_hash = createContentHash(message, sign_body);
  auto& headers = message.headers();
  sign(headers, content_hash, override_region);
}

void SignerImpl::signEmptyPayload(Http::RequestHeaderMap& headers,
                                  const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256,
                       SignatureConstants::get().HashedEmptyString);
  sign(headers, SignatureConstants::get().HashedEmptyString, override_region);
}

void SignerImpl::signUnsignedPayload(Http::RequestHeaderMap& headers,
                                     const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256,
                       SignatureConstants::get().UnsignedPayload);
  sign(headers, SignatureConstants::get().UnsignedPayload, override_region);
}

void SignerImpl::sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
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
  const auto signature = createSignature(credentials.secretAccessKey().value(), short_date,
                                         string_to_sign, override_region);
  // Phase 4: Sign request
  const auto authorization_header = createAuthorizationHeader(
      credentials.accessKeyId().value(), credential_scope, canonical_headers, signature);
  ENVOY_LOG(debug, "Signing request with: {}", authorization_header);
  headers.addCopy(Http::CustomHeaders::get().Authorization, authorization_header);
}

std::string SignerImpl::createContentHash(Http::RequestMessage& message, bool sign_body) const {
  if (!sign_body) {
    return SignatureConstants::get().HashedEmptyString;
  }
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto content_hash = message.body().length() > 0
                                ? Hex::encode(crypto_util.getSha256Digest(message.body()))
                                : SignatureConstants::get().HashedEmptyString;
  return content_hash;
}

std::string SignerImpl::createCredentialScope(absl::string_view short_date,
                                              absl::string_view override_region) const {
  return fmt::format(fmt::runtime(SignatureConstants::get().SigV4CredentialScopeFormat), short_date,
                     override_region.empty() ? region_ : override_region, service_name_);
}

std::string SignerImpl::createStringToSign(absl::string_view canonical_request,
                                           absl::string_view long_date,
                                           absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  return fmt::format(
      fmt::runtime(SignatureConstants::get().SigV4StringToSignFormat), long_date, credential_scope,
      Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
}

std::string SignerImpl::createSignature(absl::string_view secret_access_key,
                                        absl::string_view short_date,
                                        absl::string_view string_to_sign,
                                        absl::string_view override_region) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto secret_key =
      absl::StrCat(SignatureConstants::get().SigV4SignatureVersion, secret_access_key);
  const auto date_key = crypto_util.getSha256Hmac(
      std::vector<uint8_t>(secret_key.begin(), secret_key.end()), short_date);
  const auto region_key =
      crypto_util.getSha256Hmac(date_key, override_region.empty() ? region_ : override_region);
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
  return fmt::format(fmt::runtime(SignatureConstants::get().AuthorizationHeaderFormat),
                     access_key_id, credential_scope, signed_headers, signature);
}

void SigV4ASignerImpl::sign(Http::RequestMessage& message, bool sign_body,
                            const absl::string_view override_region) {
  const auto content_hash = createContentHash(message, sign_body);
  auto& headers = message.headers();
  sign(headers, content_hash, override_region);
}

void SigV4ASignerImpl::signEmptyPayload(Http::RequestHeaderMap& headers,
                                        const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256,
                       SignatureConstants::get().HashedEmptyString);
  sign(headers, SignatureConstants::get().HashedEmptyString, override_region);
}

void SigV4ASignerImpl::signUnsignedPayload(Http::RequestHeaderMap& headers,
                                           const absl::string_view override_region) {
  headers.setReference(SignatureHeaders::get().ContentSha256,
                       SignatureConstants::get().UnsignedPayload);
  sign(headers, SignatureConstants::get().UnsignedPayload, override_region);
}

void SigV4ASignerImpl::sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
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
  headers.addCopy(SignatureHeaders::get().RegionSet,
                  override_region.empty() ? region_ : override_region);

  // Phase 1: Create a canonical request
  const auto canonical_headers = Utility::canonicalizeHeaders(headers, excluded_header_matchers_);
  const auto canonical_request = Utility::createCanonicalRequest(
      service_name_, method_header->value().getStringView(), path_header->value().getStringView(),
      canonical_headers, content_hash);
  ENVOY_LOG(debug, "Canonical request:\n{}", canonical_request);
  // Phase 2: Create a string to sign
  const auto credential_scope = createCredentialScope(short_date);
  const auto string_to_sign = createStringToSign(canonical_request, long_date, credential_scope);
  ENVOY_LOG(debug, "String to sign:\n{}", string_to_sign);
  // Phase 3: Create a signature
  const auto signature = createSignature(credentials.accessKeyId().value(), credentials.secretAccessKey().value(), 
                                                            short_date, string_to_sign, override_region);
  // Phase 4: Sign request
  const auto authorization_header = createAuthorizationHeader(
      credentials.accessKeyId().value(), credential_scope, canonical_headers, signature);
  ENVOY_LOG(debug, "Signing request with: {}", authorization_header);
  headers.addCopy(Http::CustomHeaders::get().Authorization, authorization_header);
}

std::string SigV4ASignerImpl::createContentHash(Http::RequestMessage& message,
                                                bool sign_body) const {
  if (!sign_body) {
    return SignatureConstants::get().HashedEmptyString;
  }
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto content_hash = message.body().length() > 0
                                ? Hex::encode(crypto_util.getSha256Digest(message.body()))
                                : SignatureConstants::get().HashedEmptyString;
  return content_hash;
}

std::string SigV4ASignerImpl::createCredentialScope(absl::string_view short_date) const {
  return fmt::format(fmt::runtime(SignatureConstants::get().SigV4ACredentialScopeFormat),
                     short_date, service_name_);
}

std::string SigV4ASignerImpl::createStringToSign(absl::string_view canonical_request,
                                                 absl::string_view long_date,
                                                 absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  return fmt::format(
      fmt::runtime(SignatureConstants::get().SigV4AStringToSignFormat), long_date, credential_scope,
      Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
}

std::string SigV4ASignerImpl::createSignature(absl::string_view access_key_id,
                                              absl::string_view secret_access_key,
                                              absl::string_view short_date,
                                              absl::string_view string_to_sign,
                                              absl::string_view override_region) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  // AWS SigV4A Key Derivation

  const uint8_t key_length = 32; // AWS_CAL_ECDSA_P256
  std::vector<uint8_t> private_key_buf(key_length);

  const uint8_t access_key_length = access_key_id.length();
  const uint8_t required_fixed_input_length = 32 + access_key_length;
  std::vector<uint8_t> fixed_input(required_fixed_input_length);

  std::vector<uint8_t> fixed_input_hmac_digest(32); // AWS_SHA256_LEN

  const auto secret_key =
  absl::StrCat(SignatureConstants::get().SigV4ASignatureVersion, secret_access_key);
  auto k0 = crypto_util.getSha256Hmac(
    std::vector<uint8_t>(secret_key.begin(), secret_key.end()), fixed_input);

  enum SigV4AKeyDerivationResult result = AkdrNextCounter;
  uint8_t external_counter = 1;

  while ((result == AkdrNextCounter) && (external_counter <= 254))  // MAX_KEY_DERIVATION_COUNTER_VALUE
  {
    fixed_input.clear();
    fixed_input.insert(fixed_input.begin(),{0x00,0x00,0x00,0x01});
    fixed_input.insert(fixed_input.end(),SignatureConstants::get().SigV4ALabel.begin(),SignatureConstants::get().SigV4ALabel.end());
    fixed_input.insert(fixed_input.end(), 0x00);
    fixed_input.insert(fixed_input.end(), access_key_id.begin(), access_key_id.end());
    fixed_input.insert(fixed_input.end(), external_counter);
    fixed_input.insert(fixed_input.end(), {0x00,0x00,0x01,0x00});

    fixed_input_hmac_digest.clear();
    ENVOY_LOG(debug, "got hmac {}",Hex::encode(k0));
    ENVOY_LOG(debug, "k0 size {}",k0.size());

    // ASSERT(k0.size()==32);

    // ECDSA q - 2
    std::vector<uint8_t> s_n_minus_2 = {
      0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84,
      0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x4F,
    };

    ASSERT(k0.size() == s_n_minus_2.size());

    // check that k0 < 
    bool lt_result = constantTimeLessThanOrEqualTo(k0, s_n_minus_2);
    if(!lt_result)
    {
      ENVOY_LOG(debug, "comparison_result = {}, counter = {}, looping",lt_result,external_counter);
      external_counter++;
    }
    else {
      result=SigV4AKeyDerivationResult::AkdrSuccess;
      ENVOY_LOG(debug, "comparison_result = {}, ending loop",lt_result);
      // PrivateKey d = c+1
      constantTimeAddOne(k0);
    }
    
  }

  auto blank = std::string();

  if(result==SigV4AKeyDerivationResult::AkdrNextCounter) 
  {
    ENVOY_LOG(debug, "Key derivation exceeded retries, returning no signature");
    return blank;
  }
  

  EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  
  BIGNUM *priv_key_num = BN_bin2bn(k0.data(), k0.size(), nullptr);
  if (!EC_KEY_set_private_key(ec_key, priv_key_num)) {
      ENVOY_LOG(debug, "Failed to set openssl private key");
      BN_free(priv_key_num);
      return blank;
  }
  BN_free(priv_key_num);
  std::vector<uint8_t> signature(1000,0);
  uint signature_size;

  const uint8_t *stringChar = new uint8_t[string_to_sign.size()];
  std::copy(string_to_sign.begin(),string_to_sign.end(),stringChar);

  ECDSA_sign(
        0,
        stringChar,
        string_to_sign.size(),
        signature.data(),
        &signature_size,
        ec_key);
  ENVOY_LOG(debug, "calculated signature {}",Hex::encode(signature));

  // const auto secret_key =
  //     absl::StrCat(SignatureConstants::get().SigV4ASignatureVersion, secret_access_key);
  const auto date_key = crypto_util.getSha256Hmac(
      std::vector<uint8_t>(secret_key.begin(), secret_key.end()), short_date);
  const auto region_key =
      crypto_util.getSha256Hmac(date_key, override_region.empty() ? region_ : override_region);
  const auto service_key = crypto_util.getSha256Hmac(region_key, service_name_);
  const auto signing_key =
      crypto_util.getSha256Hmac(service_key, SignatureConstants::get().Aws4Request);
  return Hex::encode(crypto_util.getSha256Hmac(signing_key, string_to_sign));
}

std::string SigV4ASignerImpl::createAuthorizationHeader(
    absl::string_view access_key_id, absl::string_view credential_scope,
    const std::map<std::string, std::string>& canonical_headers,
    absl::string_view signature) const {
  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);
  return fmt::format(fmt::runtime(SignatureConstants::get().AuthorizationHeaderFormat),
                     access_key_id, credential_scope, signed_headers, signature);
}

  // adapted from https://github.com/awslabs/aws-c-auth/blob/baeffa791d9d1cf61460662a6d9ac2186aaf05df/source/key_derivation.c#L152

bool SigV4ASignerImpl::constantTimeLessThanOrEqualTo(
  std::vector<uint8_t> lhs_raw_be_bigint,
  std::vector<uint8_t> rhs_raw_be_bigint) const
{

    volatile uint8_t gt = 0;
    volatile uint8_t eq = 1;

    for (uint8_t i = 0; i < lhs_raw_be_bigint.size(); ++i) {
        volatile int32_t lhs_digit = lhs_raw_be_bigint[i];
        volatile int32_t rhs_digit = rhs_raw_be_bigint[i];

        gt |= ((rhs_digit - lhs_digit) >> 31) & eq;
        eq &= (((lhs_digit ^ rhs_digit) - 1) >> 31) & 0x01;
    }
    return (gt + gt + eq - 1)<=0;

}

void SigV4ASignerImpl::constantTimeAddOne(std::vector<uint8_t> raw_be_bigint) const {

    const uint8_t byte_count = raw_be_bigint.size();

    volatile uint32_t carry = 1;

    for (size_t i = 0; i < byte_count; ++i) {
        const size_t index = byte_count - i - 1;

        volatile uint32_t current_digit = raw_be_bigint[index];
        current_digit += carry;

        carry = (current_digit >> 8) & 0x01;

        raw_be_bigint[index] = (current_digit & 0xFF);
    }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
