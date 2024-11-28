#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "openssl/evp.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * AWS credentials containers
 *
 * If a credential component was not found in the execution environment, it's getter method will
 * return absl::nullopt. Credential components with the empty string value are treated as not found.
 */
class Credentials {
public:
  // Access Key Credentials
  explicit Credentials(absl::string_view access_key_id = absl::string_view(),
                       absl::string_view secret_access_key = absl::string_view(),
                       absl::string_view session_token = absl::string_view()) {
    // TODO(suniltheta): Move credential expiration date in here
    if (!access_key_id.empty()) {
      access_key_id_ = std::string(access_key_id);
      if (!secret_access_key.empty()) {
        secret_access_key_ = std::string(secret_access_key);
        if (!session_token.empty()) {
          session_token_ = std::string(session_token);
        }
      }
    }
  }

  const absl::optional<std::string>& accessKeyId() const { return access_key_id_; }

  const absl::optional<std::string>& secretAccessKey() const { return secret_access_key_; }

  const absl::optional<std::string>& sessionToken() const { return session_token_; }

  bool operator==(const Credentials& other) const {
    return access_key_id_ == other.access_key_id_ &&
           secret_access_key_ == other.secret_access_key_ && session_token_ == other.session_token_;
  }

private:
  absl::optional<std::string> access_key_id_;
  absl::optional<std::string> secret_access_key_;
  absl::optional<std::string> session_token_;
};

/*
 * X509 Credentials used for IAM Roles Anywhere
 */

class X509Credentials {
public:
  enum class PublicKeySignatureAlgorithm {
    RSA,
    ECDSA,
  };

  // X509 Credentials
  X509Credentials(std::string certificate_b64,
                  PublicKeySignatureAlgorithm certificate_signature_algorithm,
                  std::string certificate_serial, absl::optional<std::string> certificate_chain_b64,
                  std::string certificate_private_key_pem, SystemTime certificate_expiration_time)
      : certificate_b64_(certificate_b64),
        certificate_private_key_pem_(certificate_private_key_pem),
        certificate_serial_(certificate_serial),
        certificate_expiration_(certificate_expiration_time),
        certificate_signature_algorithm_(certificate_signature_algorithm) {
    if (certificate_chain_b64.has_value()) {
      certificate_chain_b64_ = certificate_chain_b64.value();
    }
  }

  X509Credentials() = default;

  const absl::optional<std::string>& certificateDerB64() const { return certificate_b64_; }

  const absl::optional<std::string>& certificateSerial() const { return certificate_serial_; }

  const absl::optional<SystemTime>& certificateExpiration() const {
    return certificate_expiration_;
  }

  const absl::optional<std::string>& certificateChainDerB64() const {
    return certificate_chain_b64_;
  }

  const absl::optional<PublicKeySignatureAlgorithm>& publicKeySignatureAlgorithm() const {
    return certificate_signature_algorithm_;
  }

  const absl::optional<std::string> certificatePrivateKey() const {
    return certificate_private_key_pem_;
  }

private:
  // RolesAnywhere certificate based credentials
  absl::optional<std::string> certificate_b64_ = absl::nullopt;
  absl::optional<std::string> certificate_chain_b64_ = absl::nullopt;
  absl::optional<std::string> certificate_private_key_pem_ = absl::nullopt;
  absl::optional<std::string> certificate_serial_ = absl::nullopt;
  absl::optional<SystemTime> certificate_expiration_ = absl::nullopt;
  absl::optional<PublicKeySignatureAlgorithm> certificate_signature_algorithm_ = absl::nullopt;
};

/*
 * Interface for classes able to fetch AWS credentials from the execution environment.
 */
class CredentialsProvider {
public:
  virtual ~CredentialsProvider() = default;

  /**
   * Get credentials from the environment.
   *
   * @return AWS credentials
   */
  virtual Credentials getCredentials() PURE;
};

using CredentialsConstSharedPtr = std::shared_ptr<const Credentials>;
using CredentialsConstUniquePtr = std::unique_ptr<const Credentials>;
using CredentialsProviderSharedPtr = std::shared_ptr<CredentialsProvider>;

/*
 * X509 Credentials used for IAM Roles Anywhere
 */
class X509CredentialsProvider {
public:
  virtual ~X509CredentialsProvider() = default;

  /**
   * Get credentials from the environment.
   *
   * @return AWS credentials
   */
  virtual X509Credentials getCredentials() PURE;
};

using X509CredentialsProviderSharedPtr = std::shared_ptr<X509CredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
