#pragma once

#include <functional>
#include <memory>

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/common/jwks/v3/jwks.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/jwt/jwks.h"
#include "source/extensions/filters/http/common/jwks_fetcher.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Jwks {

// JWKS shared read-only across worker threads for token verification.
using JwksConstSharedPtr = std::shared_ptr<const JwtVerify::Jwks>;

// Creates a JwksFetcher for a remote_jwks source. Tests inject one to supply canned keys.
using JwksFetcherFactory = std::function<Extensions::HttpFilters::Common::JwksFetcherPtr(
    Upstream::ClusterManager&, Router::RetryPolicyConstSharedPtr,
    const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks&)>;

// Supplies the JWKS currently used to verify a token. The keys may be inline or fetched in the
// background, and callers read them the same way.
class JwksProvider {
public:
  virtual ~JwksProvider() = default;

  // The keys currently in effect, or nullptr if none are ready yet. A remote source is nullptr
  // until its first successful fetch.
  virtual JwksConstSharedPtr jwks() const PURE;
};

using JwksProviderSharedPtr = std::shared_ptr<JwksProvider>;

// Inline JWKS parsed once from a data source. The keys never change after construction.
class StaticJwksProvider : public JwksProvider {
public:
  explicit StaticJwksProvider(JwksConstSharedPtr jwks) : jwks_(std::move(jwks)) {}

  JwksConstSharedPtr jwks() const override { return jwks_; }

private:
  const JwksConstSharedPtr jwks_;
};

// Reads and parses an inline JWKS data source into a StaticJwksProvider. Returns an error for an
// unreadable data source or an invalid JWKS.
absl::StatusOr<JwksProviderSharedPtr>
createStaticJwksProvider(const envoy::config::core::v3::DataSource& data_source, Api::Api& api);

// Starts a background fetcher for a remote JWKS and returns a provider over the fetched keys.
// Returns an error for a bad URI or retry policy. The counters track fetch outcomes and must
// outlive the provider, so pass scope-owned counters.
absl::StatusOr<JwksProviderSharedPtr> createRemoteJwksProvider(
    const envoy::extensions::filters::common::jwks::v3::RemoteJwks& remote_jwks,
    Server::Configuration::FactoryContext& context, JwksFetcherFactory create_fetcher_fn,
    Stats::Counter& fetch_success, Stats::Counter& fetch_failed);

// Builds a router retry policy from a core retry policy. Returns an error for an invalid policy.
// The caller checks whether one is set before calling.
absl::StatusOr<Router::RetryPolicyConstSharedPtr>
buildRetryPolicy(const envoy::config::core::v3::RetryPolicy& retry_policy,
                 Server::Configuration::ServerFactoryContext& server_context);

} // namespace Jwks
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
