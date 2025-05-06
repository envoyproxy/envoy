#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/thread.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr char AWS_ACCESS_KEY_ID[] = "AWS_ACCESS_KEY_ID";
constexpr char AWS_SECRET_ACCESS_KEY[] = "AWS_SECRET_ACCESS_KEY";
constexpr char AWS_SESSION_TOKEN[] = "AWS_SESSION_TOKEN";
constexpr std::chrono::hours REFRESH_INTERVAL{1};

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

  bool hasCredentials() const {
    return access_key_id_.has_value() && secret_access_key_.has_value();
  }

  bool operator==(const Credentials& other) const {
    return access_key_id_ == other.access_key_id_ &&
           secret_access_key_ == other.secret_access_key_ && session_token_ == other.session_token_;
  }

private:
  absl::optional<std::string> access_key_id_;
  absl::optional<std::string> secret_access_key_;
  absl::optional<std::string> session_token_;
};

using CredentialsPendingCallback = std::function<void()>;

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
  X509Credentials(absl::string_view certificate_b64,
                  PublicKeySignatureAlgorithm certificate_signature_algorithm,
                  absl::string_view certificate_serial,
                  absl::optional<absl::string_view> certificate_chain_b64,
                  absl::string_view certificate_private_key_pem,
                  SystemTime certificate_expiration_time)
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
  virtual std::string providerName() PURE;
  virtual Credentials getCredentials() PURE;
  /**
   * @return true if credentials are pending from this provider, false if credentials are available
   */
  virtual bool credentialsPending() PURE;
};

using CredentialsConstSharedPtr = std::shared_ptr<const Credentials>;
using CredentialsConstUniquePtr = std::unique_ptr<const Credentials>;
using CredentialsProviderSharedPtr = std::shared_ptr<CredentialsProvider>;

class CredentialSubscriberCallbacks {
public:
  virtual ~CredentialSubscriberCallbacks() = default;

  virtual void onCredentialUpdate() PURE;
};

// Subscription model allowing CredentialsProviderChains to be notified of credential provider
// updates. A credential provider chain will call credential_provider->subscribeToCredentialUpdates
// to register itself for updates via onCredentialUpdate callback. When a credential provider has
// successfully updated all threads with new credentials, via the setCredentialsToAllThreads method
// it will notify all subscribers that credentials have been retrieved.
// RAII is used, as credential providers may be instantiated as singletons, as such they may outlive
// the credential provider chain. Subscription is only relevant for metadata credentials providers,
// as these are the only credential providers that implement async credential retrieval
// functionality.
class CredentialSubscriberCallbacksHandle : public RaiiListElement<CredentialSubscriberCallbacks*> {
public:
  CredentialSubscriberCallbacksHandle(CredentialSubscriberCallbacks& cb,
                                      std::list<CredentialSubscriberCallbacks*>& parent)
      : RaiiListElement<CredentialSubscriberCallbacks*>(parent, &cb) {}
};

using CredentialSubscriberCallbacksHandlePtr = std::unique_ptr<CredentialSubscriberCallbacksHandle>;

/**
 * AWS credentials provider chain, able to fallback between multiple credential providers.
 */
class CredentialsProviderChain : public CredentialSubscriberCallbacks,
                                 public Logger::Loggable<Logger::Id::aws> {
public:
  ~CredentialsProviderChain() override {
    for (auto& subscriber_handle : subscriber_handles_) {
      if (subscriber_handle) {
        subscriber_handle->cancel();
      }
    }
  }

  void add(const CredentialsProviderSharedPtr& credentials_provider) {
    providers_.emplace_back(credentials_provider);
  }

  bool addCallbackIfChainCredentialsPending(CredentialsPendingCallback&&);

  Credentials chainGetCredentials();

  // Store the RAII handle for a subscription to credential provider notification
  void storeSubscription(CredentialSubscriberCallbacksHandlePtr);

private:
  // Callback to notify on credential updates occurring from a chain member
  void onCredentialUpdate() override;

  bool chainProvidersPending();

protected:
  std::list<CredentialsProviderSharedPtr> providers_;
  Thread::MutexBasicLockable mu_;
  std::vector<CredentialsPendingCallback> credential_pending_callbacks_ ABSL_GUARDED_BY(mu_);
  std::list<CredentialSubscriberCallbacksHandlePtr> subscriber_handles_;
};

using CredentialsProviderChainSharedPtr = std::shared_ptr<CredentialsProviderChain>;

/*
 * X509 credential provider used for IAM Roles Anywhere
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
