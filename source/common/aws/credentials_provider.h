#pragma once

#include <string>

#include "envoy/common/pure.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class Credentials {
public:
  Credentials() = default;
  ~Credentials() = default;

  Credentials(const std::string& access_key_id, const std::string& secret_access_key)
      : access_key_id_(access_key_id), secret_access_key_(secret_access_key) {}

  Credentials(const std::string& access_key_id, const std::string& secret_access_key,
              const std::string& session_token)
      : access_key_id_(access_key_id), secret_access_key_(secret_access_key),
        session_token_(session_token) {}

  void setAccessKeyId(const std::string& access_key_id) {
    access_key_id_ = absl::optional<std::string>(access_key_id);
  }

  const absl::optional<std::string>& accessKeyId() const { return access_key_id_; }

  void setSecretAccessKey(const std::string& secret_key) {
    secret_access_key_ = absl::optional<std::string>(secret_key);
  }

  const absl::optional<std::string>& secretAccessKey() const { return secret_access_key_; }

  void setSessionToken(const std::string& session_token) {
    session_token_ = absl::optional<std::string>(session_token);
  }

  const absl::optional<std::string>& sessionToken() const { return session_token_; }

private:
  absl::optional<std::string> access_key_id_;
  absl::optional<std::string> secret_access_key_;
  absl::optional<std::string> session_token_;
};

class CredentialsProvider {
public:
  virtual ~CredentialsProvider() = default;

  virtual Credentials getCredentials() PURE;
};

typedef std::shared_ptr<CredentialsProvider> CredentialsProviderSharedPtr;

} // namespace Auth
} // namespace Aws
} // namespace Envoy