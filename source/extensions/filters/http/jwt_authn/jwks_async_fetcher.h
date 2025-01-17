#pragma once

#include <chrono>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/init/target_impl.h"
#include "source/extensions/filters/http/common/jwks_fetcher.h"
#include "source/extensions/filters/http/jwt_authn/stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 *  CreateJwksFetcherCb is a callback interface for creating a JwksFetcher instance.
 */
using CreateJwksFetcherCb = std::function<Common::JwksFetcherPtr(
    Upstream::ClusterManager&, const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks&)>;
/**
 *  JwksDoneFetched is a callback interface to set a Jwks when fetch is done.
 */
using JwksDoneFetched = std::function<void(google::jwt_verify::JwksPtr&& jwks)>;

// This class handles fetching Jwks asynchronously.
// It will be no-op if async_fetch is not enabled.
// At its constructor, it will start to fetch Jwks, register with init_manager if not fast_listener.
// and handle fetching response. When cache is expired, it will fetch again.
// When a Jwks is fetched, done_fn is called to set the Jwks.
class JwksAsyncFetcher : public Logger::Loggable<Logger::Id::jwt>,
                         public Common::JwksFetcher::JwksReceiver {
public:
  JwksAsyncFetcher(const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks,
                   Server::Configuration::FactoryContext& context, CreateJwksFetcherCb fetcher_fn,
                   JwtAuthnFilterStats& stats, JwksDoneFetched done_fn);

  // Get the remote Jwks cache duration.
  static std::chrono::seconds
  getCacheDuration(const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks);

private:
  // Fetch the Jwks
  void fetch();
  // Handle fetch done.
  void handleFetchDone();

  // Override the functions from Common::JwksFetcher::JwksReceiver
  void onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) override;
  void onJwksError(Failure reason) override;

  // the remote Jwks config
  const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks_;
  // the factory context
  Server::Configuration::FactoryContext& context_;
  // the jwks fetcher creator function
  const CreateJwksFetcherCb create_fetcher_fn_;
  // stats
  JwtAuthnFilterStats& stats_;
  // the Jwks done function.
  const JwksDoneFetched done_fn_;

  // The Jwks fetcher object
  Common::JwksFetcherPtr fetcher_;

  // The next refetch duration after a good fetch.
  std::chrono::milliseconds good_refetch_duration_;
  // The next refetch duration after a failed fetch.
  std::chrono::milliseconds failed_refetch_duration_;
  // The timer to trigger a refetch.
  Envoy::Event::TimerPtr refetch_timer_;

  // The init target.
  std::unique_ptr<Init::TargetImpl> init_target_;

  // Used in logs.
  const std::string debug_name_;
};

using JwksAsyncFetcherPtr = std::unique_ptr<JwksAsyncFetcher>;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
