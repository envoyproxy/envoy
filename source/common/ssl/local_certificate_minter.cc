#include "source/common/ssl/local_certificate_minter.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "openssl/bio.h"
#include "openssl/base.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/rand.h"
#include "openssl/rsa.h"
#include "openssl/x509.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

namespace {

absl::StatusOr<std::string> bioToString(BIO* bio) {
  BUF_MEM* mem = nullptr;
  BIO_get_mem_ptr(bio, &mem);
  if (mem == nullptr || mem->data == nullptr || mem->length == 0) {
    return absl::InternalError("empty BIO buffer");
  }
  return std::string(mem->data, mem->length);
}

absl::StatusOr<bssl::UniquePtr<EVP_PKEY>> generateRsaKey(uint32_t key_bits) {
  bssl::UniquePtr<BIGNUM> exponent(BN_new());
  if (!exponent || BN_set_word(exponent.get(), RSA_F4) != 1) {
    return absl::InternalError("failed to initialize RSA exponent");
  }
  bssl::UniquePtr<RSA> rsa(RSA_new());
  if (!rsa || RSA_generate_key_ex(rsa.get(), key_bits, exponent.get(), nullptr) != 1) {
    return absl::InternalError("failed to generate RSA key");
  }
  bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
  if (!pkey || EVP_PKEY_set1_RSA(pkey.get(), rsa.get()) != 1) {
    return absl::InternalError("failed to wrap RSA key");
  }
  return pkey;
}

absl::StatusOr<bssl::UniquePtr<EVP_PKEY>> generateEcdsaKey(LocalCertificateMinter::EcdsaCurve curve) {
  int nid = NID_X9_62_prime256v1;
  switch (curve) {
  case LocalCertificateMinter::EcdsaCurve::P256:
    nid = NID_X9_62_prime256v1;
    break;
  case LocalCertificateMinter::EcdsaCurve::P384:
    nid = NID_secp384r1;
    break;
  }

  bssl::UniquePtr<EC_KEY> ec_key(EC_KEY_new_by_curve_name(nid));
  if (!ec_key || EC_KEY_generate_key(ec_key.get()) != 1) {
    return absl::InternalError("failed to generate ECDSA key");
  }

  bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
  if (!pkey || EVP_PKEY_set1_EC_KEY(pkey.get(), ec_key.get()) != 1) {
    return absl::InternalError("failed to wrap ECDSA key");
  }
  return pkey;
}

absl::StatusOr<const EVP_MD*> digestForSignatureHash(LocalCertificateMinter::SignatureHash signature_hash) {
  switch (signature_hash) {
  case LocalCertificateMinter::SignatureHash::Sha256:
    return EVP_sha256();
  case LocalCertificateMinter::SignatureHash::Sha384:
    return EVP_sha384();
  case LocalCertificateMinter::SignatureHash::Sha512:
    return EVP_sha512();
  }
  return absl::InvalidArgumentError("unsupported signature hash");
}

absl::StatusOr<bssl::UniquePtr<EVP_PKEY>>
generateLeafKey(LocalCertificateMinter::KeyType key_type, uint32_t rsa_key_bits,
                LocalCertificateMinter::EcdsaCurve ecdsa_curve) {
  switch (key_type) {
  case LocalCertificateMinter::KeyType::Rsa:
    return generateRsaKey(rsa_key_bits);
  case LocalCertificateMinter::KeyType::Ecdsa:
    return generateEcdsaKey(ecdsa_curve);
  }
  return absl::InvalidArgumentError("unsupported key type");
}

absl::Status addExtension(X509* cert, X509* issuer, int nid, absl::string_view value) {
  X509V3_CTX ctx;
  X509V3_set_ctx(&ctx, issuer, cert, nullptr, nullptr, 0);
  bssl::UniquePtr<X509_EXTENSION> ext(
      X509V3_EXT_nconf_nid(nullptr, &ctx, nid, std::string(value).c_str()));
  if (!ext || X509_add_ext(cert, ext.get(), -1) != 1) {
    return absl::InternalError("failed to add X509 extension");
  }
  return absl::OkStatus();
}

absl::Status addDnsSans(X509* cert, X509* issuer, const std::vector<std::string>& dns_names) {
  if (dns_names.empty()) {
    return absl::OkStatus();
  }
  std::vector<std::string> san_entries;
  san_entries.reserve(dns_names.size());
  for (const auto& name : dns_names) {
    if (!name.empty()) {
      san_entries.push_back(absl::StrCat("DNS:", name));
    }
  }
  if (san_entries.empty()) {
    return absl::OkStatus();
  }
  return addExtension(cert, issuer, NID_subject_alt_name, absl::StrJoin(san_entries, ","));
}

absl::Status addSubjectNameEntry(X509_NAME* subject, const char* field, absl::string_view value) {
  if (value.empty()) {
    return absl::OkStatus();
  }
  if (!subject ||
      X509_NAME_add_entry_by_txt(subject, field, MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char*>(std::string(value).c_str()),
                                 -1, -1, 0) != 1) {
    return absl::InternalError("failed to set subject attribute");
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::optional<std::string>>
keyUsageToken(LocalCertificateMinter::KeyUsage usage) {
  switch (usage) {
  case LocalCertificateMinter::KeyUsage::DigitalSignature:
    return std::string("digitalSignature");
  case LocalCertificateMinter::KeyUsage::ContentCommitment:
    return std::string("nonRepudiation");
  case LocalCertificateMinter::KeyUsage::KeyEncipherment:
    return std::string("keyEncipherment");
  case LocalCertificateMinter::KeyUsage::DataEncipherment:
    return std::string("dataEncipherment");
  case LocalCertificateMinter::KeyUsage::KeyAgreement:
    return std::string("keyAgreement");
  case LocalCertificateMinter::KeyUsage::KeyCertSign:
    return std::string("keyCertSign");
  case LocalCertificateMinter::KeyUsage::CrlSign:
    return std::string("cRLSign");
  }
  return absl::InvalidArgumentError("unsupported key usage");
}

absl::StatusOr<absl::optional<std::string>>
extendedKeyUsageToken(LocalCertificateMinter::ExtendedKeyUsage usage) {
  switch (usage) {
  case LocalCertificateMinter::ExtendedKeyUsage::ServerAuth:
    return std::string("serverAuth");
  case LocalCertificateMinter::ExtendedKeyUsage::ClientAuth:
    return std::string("clientAuth");
  case LocalCertificateMinter::ExtendedKeyUsage::CodeSigning:
    return std::string("codeSigning");
  case LocalCertificateMinter::ExtendedKeyUsage::EmailProtection:
    return std::string("emailProtection");
  case LocalCertificateMinter::ExtendedKeyUsage::TimeStamping:
    return std::string("timeStamping");
  case LocalCertificateMinter::ExtendedKeyUsage::OcspSigning:
    return std::string("OCSPSigning");
  }
  return absl::InvalidArgumentError("unsupported extended key usage");
}

absl::StatusOr<int64_t> randomPositiveSerial() {
  uint64_t raw = 0;
  if (RAND_bytes(reinterpret_cast<uint8_t*>(&raw), sizeof(raw)) != 1) {
    return absl::InternalError("failed to generate serial");
  }
  raw = raw & 0x7fffffffULL;
  if (raw == 0) {
    raw = 1;
  }
  return static_cast<int64_t>(raw);
}

class OpenSslLocalCertificateMinter : public LocalCertificateMinter {
public:
  absl::Status validateCaMaterial(absl::string_view ca_cert_pem,
                                  absl::string_view ca_key_pem) const override {
    bssl::UniquePtr<BIO> ca_cert_bio(BIO_new_mem_buf(ca_cert_pem.data(), ca_cert_pem.size()));
    bssl::UniquePtr<BIO> ca_key_bio(BIO_new_mem_buf(ca_key_pem.data(), ca_key_pem.size()));
    if (!ca_cert_bio || !ca_key_bio) {
      return absl::InternalError("failed to allocate CA BIO");
    }
    bssl::UniquePtr<X509> ca_cert(PEM_read_bio_X509(ca_cert_bio.get(), nullptr, nullptr, nullptr));
    bssl::UniquePtr<EVP_PKEY> ca_key(PEM_read_bio_PrivateKey(ca_key_bio.get(), nullptr, nullptr, nullptr));
    if (!ca_cert || !ca_key) {
      return absl::InvalidArgumentError("failed to parse CA cert/key PEM");
    }
    return absl::OkStatus();
  }

  absl::StatusOr<MintedCertificate> mint(const MintRequest& request) const override {
    bssl::UniquePtr<BIO> ca_cert_bio(BIO_new_mem_buf(request.ca_cert_pem.data(), request.ca_cert_pem.size()));
    bssl::UniquePtr<BIO> ca_key_bio(BIO_new_mem_buf(request.ca_key_pem.data(), request.ca_key_pem.size()));
    if (!ca_cert_bio || !ca_key_bio) {
      return absl::InternalError("failed to allocate CA BIO");
    }

    bssl::UniquePtr<X509> ca_cert(PEM_read_bio_X509(ca_cert_bio.get(), nullptr, nullptr, nullptr));
    bssl::UniquePtr<EVP_PKEY> ca_key(PEM_read_bio_PrivateKey(ca_key_bio.get(), nullptr, nullptr, nullptr));
    if (!ca_cert || !ca_key) {
      return absl::InvalidArgumentError("failed to parse CA cert/key PEM");
    }

    auto leaf_key_or = generateLeafKey(request.key_type, request.rsa_key_bits, request.ecdsa_curve);
    if (!leaf_key_or.ok()) {
      return leaf_key_or.status();
    }
    bssl::UniquePtr<EVP_PKEY> leaf_key = std::move(leaf_key_or.value());

    bssl::UniquePtr<X509> cert(X509_new());
    if (!cert || X509_set_version(cert.get(), 2) != 1) {
      return absl::InternalError("failed to initialize leaf certificate");
    }
    auto serial_or = randomPositiveSerial();
    if (!serial_or.ok()) {
      return serial_or.status();
    }
    if (ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial_or.value()) != 1) {
      return absl::InternalError("failed to set leaf serial");
    }
    if (X509_gmtime_adj(X509_getm_notBefore(cert.get()),
                        -static_cast<int64_t>(request.not_before_backdate_seconds)) == nullptr ||
        X509_gmtime_adj(X509_getm_notAfter(cert.get()),
                        static_cast<int64_t>(request.ttl_days) * 24 * 60 * 60) == nullptr) {
      return absl::InternalError("failed to set certificate validity");
    }
    if (X509_set_issuer_name(cert.get(), X509_get_subject_name(ca_cert.get())) != 1 ||
        X509_set_pubkey(cert.get(), leaf_key.get()) != 1) {
      return absl::InternalError("failed to set issuer/public key");
    }

    X509_NAME* subject = X509_get_subject_name(cert.get());
    const auto cn = request.subject_common_name.empty() ? request.host_name : request.subject_common_name;
    if (const auto status = addSubjectNameEntry(subject, "CN", cn); !status.ok()) {
      return status;
    }
    if (const auto status = addSubjectNameEntry(subject, "O", request.subject_organization);
        !status.ok()) {
      return status;
    }
    if (const auto status =
            addSubjectNameEntry(subject, "OU", request.subject_organizational_unit);
        !status.ok()) {
      return status;
    }
    if (const auto status = addSubjectNameEntry(subject, "C", request.subject_country);
        !status.ok()) {
      return status;
    }
    if (const auto status =
            addSubjectNameEntry(subject, "ST", request.subject_state_or_province);
        !status.ok()) {
      return status;
    }
    if (const auto status = addSubjectNameEntry(subject, "L", request.subject_locality);
        !status.ok()) {
      return status;
    }

    if (const auto status = addDnsSans(cert.get(), ca_cert.get(), request.dns_sans);
        !status.ok()) {
      return status;
    }
    if (!request.key_usages.empty()) {
      std::vector<std::string> key_usage_tokens;
      key_usage_tokens.reserve(request.key_usages.size());
      for (const auto usage : request.key_usages) {
        auto token_or = keyUsageToken(usage);
        if (!token_or.ok()) {
          return token_or.status();
        }
        if (token_or.value().has_value()) {
          key_usage_tokens.push_back(token_or.value().value());
        }
      }
      if (!key_usage_tokens.empty()) {
        if (const auto status =
                addExtension(cert.get(), ca_cert.get(), NID_key_usage,
                             absl::StrCat("critical,", absl::StrJoin(key_usage_tokens, ",")));
            !status.ok()) {
          return status;
        }
      }
    }
    if (!request.extended_key_usages.empty()) {
      std::vector<std::string> eku_tokens;
      eku_tokens.reserve(request.extended_key_usages.size());
      for (const auto usage : request.extended_key_usages) {
        auto token_or = extendedKeyUsageToken(usage);
        if (!token_or.ok()) {
          return token_or.status();
        }
        if (token_or.value().has_value()) {
          eku_tokens.push_back(token_or.value().value());
        }
      }
      if (!eku_tokens.empty()) {
        if (const auto status = addExtension(cert.get(), ca_cert.get(), NID_ext_key_usage,
                                             absl::StrJoin(eku_tokens, ","));
            !status.ok()) {
          return status;
        }
      }
    }
    if (request.basic_constraints_ca.has_value()) {
      if (const auto status = addExtension(cert.get(), ca_cert.get(), NID_basic_constraints,
                                           request.basic_constraints_ca.value()
                                               ? "critical,CA:TRUE"
                                               : "critical,CA:FALSE");
          !status.ok()) {
        return status;
      }
    }
    auto digest_or = digestForSignatureHash(request.signature_hash);
    if (!digest_or.ok()) {
      return digest_or.status();
    }
    if (X509_sign(cert.get(), ca_key.get(), digest_or.value()) <= 0) {
      return absl::InternalError("failed to sign leaf certificate");
    }

    bssl::UniquePtr<BIO> cert_bio(BIO_new(BIO_s_mem()));
    bssl::UniquePtr<BIO> key_bio(BIO_new(BIO_s_mem()));
    if (!cert_bio || !key_bio || PEM_write_bio_X509(cert_bio.get(), cert.get()) != 1 ||
        PEM_write_bio_PrivateKey(key_bio.get(), leaf_key.get(), nullptr, nullptr, 0, nullptr,
                                 nullptr) != 1) {
      return absl::InternalError("failed to write leaf certificate/key PEM");
    }
    auto cert_pem_or = bioToString(cert_bio.get());
    if (!cert_pem_or.ok()) {
      return cert_pem_or.status();
    }
    auto key_pem_or = bioToString(key_bio.get());
    if (!key_pem_or.ok()) {
      return key_pem_or.status();
    }

    return MintedCertificate{std::move(cert_pem_or.value()), std::move(key_pem_or.value())};
  }
};

} // namespace

LocalCertificateMinterSharedPtr getDefaultLocalCertificateMinter() {
  static const LocalCertificateMinterSharedPtr minter =
      std::make_shared<OpenSslLocalCertificateMinter>();
  return minter;
}

} // namespace Ssl
} // namespace Envoy
