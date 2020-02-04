#pragma once

#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * AWS credentials container
 *
 * If a credential component was not found in the execution environment, it's getter method will
 * return absl::nullopt. Credential components with the empty string value are treated as not found.
 */
class Credentials {
public:
  Credentials(absl::string_view access_key_id = absl::string_view(),
              absl::string_view secret_access_key = absl::string_view(),
              absl::string_view session_token = absl::string_view()) {
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

/**
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

using CredentialsProviderSharedPtr = std::shared_ptr<CredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
