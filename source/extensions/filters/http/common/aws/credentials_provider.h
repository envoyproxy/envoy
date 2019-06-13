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

class Credentials {
public:
  Credentials() = default;

  Credentials(absl::string_view access_key_id, absl::string_view secret_access_key)
      : access_key_id_(access_key_id), secret_access_key_(secret_access_key) {}

  Credentials(absl::string_view access_key_id, absl::string_view secret_access_key,
              absl::string_view session_token)
      : access_key_id_(access_key_id), secret_access_key_(secret_access_key),
        session_token_(session_token) {}

  const absl::optional<std::string>& accessKeyId() const { return access_key_id_; }

  const absl::optional<std::string>& secretAccessKey() const { return secret_access_key_; }

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

using CredentialsProviderSharedPtr = std::shared_ptr<CredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy