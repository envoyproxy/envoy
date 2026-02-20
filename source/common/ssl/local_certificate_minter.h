#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Ssl {

class LocalCertificateMinter {
public:
  enum class KeyType { Rsa, Ecdsa };
  enum class EcdsaCurve { P256, P384 };
  enum class SignatureHash { Sha256, Sha384, Sha512 };
  enum class KeyUsage {
    DigitalSignature,
    ContentCommitment,
    KeyEncipherment,
    DataEncipherment,
    KeyAgreement,
    KeyCertSign,
    CrlSign,
  };
  enum class ExtendedKeyUsage {
    ServerAuth,
    ClientAuth,
    CodeSigning,
    EmailProtection,
    TimeStamping,
    OcspSigning,
  };

  struct MintRequest {
    std::string ca_cert_pem;
    std::string ca_key_pem;
    std::string host_name;
    std::string subject_organization;
    uint32_t ttl_days;
    KeyType key_type;
    uint32_t rsa_key_bits;
    EcdsaCurve ecdsa_curve;
    SignatureHash signature_hash;
    uint32_t not_before_backdate_seconds;
    std::string subject_common_name;
    std::string subject_organizational_unit;
    std::string subject_country;
    std::string subject_state_or_province;
    std::string subject_locality;
    std::vector<std::string> dns_sans;
    std::vector<KeyUsage> key_usages;
    std::vector<ExtendedKeyUsage> extended_key_usages;
    absl::optional<bool> basic_constraints_ca;
  };

  struct MintedCertificate {
    std::string certificate_pem;
    std::string private_key_pem;
  };

  virtual ~LocalCertificateMinter() = default;

  virtual absl::Status validateCaMaterial(absl::string_view ca_cert_pem,
                                          absl::string_view ca_key_pem) const PURE;
  virtual absl::StatusOr<MintedCertificate> mint(const MintRequest& request) const PURE;
};

using LocalCertificateMinterSharedPtr = std::shared_ptr<const LocalCertificateMinter>;

LocalCertificateMinterSharedPtr getDefaultLocalCertificateMinter();

} // namespace Ssl
} // namespace Envoy
