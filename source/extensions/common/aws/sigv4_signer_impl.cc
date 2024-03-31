#include "source/extensions/common/aws/sigv4_signer_impl.h"

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

std::string SigV4SignerImpl::createCredentialScope(absl::string_view short_date,
                                                   absl::string_view override_region) const {
  return fmt::format(SigV4SignatureConstants::SigV4CredentialScopeFormat, short_date,
                     override_region.empty() ? region_ : override_region, service_name_);
}

std::string SigV4SignerImpl::createStringToSign(absl::string_view canonical_request,
                                                absl::string_view long_date,
                                                absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  return fmt::format(
      SigV4SignatureConstants::SigV4StringToSignFormat, SigV4SignatureConstants::SigV4Algorithm,
      long_date, credential_scope,
      Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
}

std::string SigV4SignerImpl::createSignature(
    ABSL_ATTRIBUTE_UNUSED const absl::string_view access_key_id,
    const absl::string_view secret_access_key, const absl::string_view short_date,
    const absl::string_view string_to_sign, const absl::string_view override_region) const {

  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto secret_key =
      absl::StrCat(SigV4SignatureConstants::SigV4SignatureVersion, secret_access_key);
  const auto date_key = crypto_util.getSha256Hmac(
      std::vector<uint8_t>(secret_key.begin(), secret_key.end()), short_date);
  const auto region_key =
      crypto_util.getSha256Hmac(date_key, override_region.empty() ? region_ : override_region);
  const auto service_key = crypto_util.getSha256Hmac(region_key, service_name_);
  const auto signing_key =
      crypto_util.getSha256Hmac(service_key, SigV4SignatureConstants::Aws4Request);
  return Hex::encode(crypto_util.getSha256Hmac(signing_key, string_to_sign));
}

std::string SigV4SignerImpl::createAuthorizationHeader(
    const absl::string_view access_key_id, const absl::string_view credential_scope,
    const std::map<std::string, std::string>& canonical_headers,
    absl::string_view signature) const {
  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);
  return fmt::format(SigV4SignatureConstants::SigV4AuthorizationHeaderFormat,
                     SigV4SignatureConstants::SigV4Algorithm,
                     createAuthorizationCredential(access_key_id, credential_scope), signed_headers,
                     signature);
}

absl::string_view SigV4SignerImpl::getAlgorithmString() const {
  return SigV4SignatureConstants::SigV4Algorithm;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
