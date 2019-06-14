#pragma once

#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
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
  Credentials() = default;

  Credentials(absl::string_view access_key_id) : access_key_id_(access_key_id) {}

  Credentials(absl::string_view access_key_id, absl::string_view secret_access_key)
      : access_key_id_(access_key_id), secret_access_key_(secret_access_key) {}

  Credentials(absl::string_view access_key_id, absl::string_view secret_access_key,
              absl::string_view session_token)
      : access_key_id_(access_key_id), secret_access_key_(secret_access_key),
        session_token_(session_token) {}

  static Credentials fromString(const std::string& access_key_id,
                                const std::string& secret_access_key,
                                const std::string& session_token) {
    if (!(access_key_id.empty() || secret_access_key.empty() || session_token.empty())) {
      return Credentials(access_key_id, secret_access_key, session_token);
    } else if (!(access_key_id.empty() || secret_access_key.empty())) {
      return Credentials(access_key_id, secret_access_key);
    } else if (!access_key_id.empty()) {
      return Credentials(access_key_id);
    }

    return Credentials();
  }

  static Credentials fromCString(const char* access_key_id, const char* secret_access_key,
                                 const char* session_token) {
    if (access_key_id && access_key_id[0] && secret_access_key && secret_access_key[0] &&
        session_token && session_token[0]) {
      return Credentials(access_key_id, secret_access_key, session_token);
    } else if (access_key_id && access_key_id[0] && secret_access_key && secret_access_key[0]) {
      return Credentials(access_key_id, secret_access_key);
    } else if (access_key_id && access_key_id[0]) {
      return Credentials(access_key_id);
    }

    return Credentials();
  }

  const absl::optional<std::string>& accessKeyId() const { return access_key_id_; }

  const absl::optional<std::string>& secretAccessKey() const { return secret_access_key_; }

  const absl::optional<std::string>& sessionToken() const { return session_token_; }

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
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
