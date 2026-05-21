#pragma once

#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "absl/status/statusor.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

struct GcpToken {
  std::string token;
  uint64_t expires_at{0}; // Expiration time in seconds since epoch.

  bool operator==(const GcpToken& other) const {
    return token == other.token && expires_at == other.expires_at;
  }
};

/**
 * Abstract interface for GcpAuthnClient.
 */
class GcpAuthnClient {
public:
  class Callbacks {
  public:
    virtual ~Callbacks() = default;

    /**
     * Called on completion of a token request.
     *
     * @param token the StatusOr containing the retrieved GcpToken or an error status.
     */
    virtual void onComplete(absl::StatusOr<GcpToken> token) PURE;
  };

  virtual ~GcpAuthnClient() = default;

  /**
   * Fetch a token.
   *
   * @param audience the Audience proto containing the request details.
   * @param callbacks the callbacks to be notified when the token fetch completes.
   */
  virtual void
  fetchToken(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
             Callbacks& callbacks) PURE;

  /**
   * Cancel the active request.
   */
  virtual void cancel() PURE;
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
