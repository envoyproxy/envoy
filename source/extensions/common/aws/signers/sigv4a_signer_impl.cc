#include "source/extensions/common/aws/signers/sigv4a_signer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

std::string SigV4ASignerImpl::createAuthorizationHeader(
    const absl::string_view access_key_id, const absl::string_view credential_scope,
    const std::map<std::string, std::string>& canonical_headers,
    absl::string_view signature) const {
  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);
  return fmt::format(SigV4ASignatureConstants::SigV4AAuthorizationHeaderFormat,
                     createAuthorizationCredential(access_key_id, credential_scope), signed_headers,
                     signature);
}

std::string SigV4ASignerImpl::createCredentialScope(
    const absl::string_view short_date,
    ABSL_ATTRIBUTE_UNUSED const absl::string_view override_region) const {
  return fmt::format(SigV4ASignatureConstants::SigV4ACredentialScopeFormat, short_date,
                     service_name_);
}

std::string SigV4ASignerImpl::createStringToSign(const absl::string_view canonical_request,
                                                 const absl::string_view long_date,
                                                 const absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  return fmt::format(
      SigV4ASignatureConstants::SigV4AStringToSignFormat, getAlgorithmString(), long_date,
      credential_scope,
      Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
}

void SigV4ASignerImpl::addRegionHeader(Http::RequestHeaderMap& headers,
                                       const absl::string_view override_region) const {
  headers.setCopy(SigV4ASignatureHeaders::get().RegionSet,
                  override_region.empty() ? getRegion() : override_region);
}

void SigV4ASignerImpl::addRegionQueryParam(Envoy::Http::Utility::QueryParamsMulti& query_params,
                                           const absl::string_view override_region) const {
  query_params.add(
      SignatureQueryParameterValues::AmzRegionSet,
      Utility::encodeQueryComponent(override_region.empty() ? getRegion() : override_region));
}

std::string SigV4ASignerImpl::createSignature(
    const absl::string_view access_key_id, const absl::string_view secret_access_key,
    ABSL_ATTRIBUTE_UNUSED const absl::string_view short_date,
    const absl::string_view string_to_sign,
    ABSL_ATTRIBUTE_UNUSED const absl::string_view override_region) const {

  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  auto ec_key_or = key_derivation_ptr_->derivePrivateKey(access_key_id, secret_access_key);
  if (!ec_key_or.ok()) {
    ENVOY_LOG(debug, "SigV4A key derivation failed {}", ec_key_or.status().message());
    return invalid_signature_;
  }

  std::vector<uint8_t> signature(ECDSA_size(ec_key_or.value()));
  unsigned int signature_size;

  // Sign the SHA256 hash of our calculated string_to_sign
  auto hash = crypto_util.getSha256Digest(Buffer::OwnedImpl(string_to_sign));

  ECDSA_sign(0, hash.data(), hash.size(), signature.data(), &signature_size, ec_key_or.value());

  EC_KEY_free(ec_key_or.value());
  std::string encoded_signature(
      Hex::encode(std::vector<uint8_t>(signature.data(), signature.data() + signature_size)));

  return encoded_signature;
}

absl::string_view SigV4ASignerImpl::getAlgorithmString() const {
  return SigV4ASignatureConstants::SigV4AAlgorithm;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
