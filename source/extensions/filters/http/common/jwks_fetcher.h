#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "jwt_verify_lib/jwks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

class JwksFetcher;
using JwksFetcherPtr = std::unique_ptr<JwksFetcher>;
/**
 * JwksFetcher interface can be used to retrieve remote JWKS
 * (https://tools.ietf.org/html/rfc7517) data structures returning a concrete,
 * type-safe representation. An instance of this interface is designed to
 * retrieve one JWKS at a time.
 */
class JwksFetcher {
public:
  class JwksReceiver {
  public:
    enum class Failure {
      /* A network error occurred causing JWKS retrieval failure. */
      Network,
      /* A failure occurred when trying to parse the retrieved JWKS data. */
      InvalidJwks,
    };

    virtual ~JwksReceiver() = default;
    /*
     * Successful retrieval callback.
     * of the returned JWKS object.
     * @param jwks the JWKS object retrieved.
     */
    virtual void onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) PURE;
    /*
     * Retrieval error callback.
     * * @param reason the failure reason.
     */
    virtual void onJwksError(Failure reason) PURE;
  };

  virtual ~JwksFetcher() = default;

  /*
   * Cancel any in-flight request.
   */
  virtual void cancel() PURE;

  /*
   * Retrieve a JWKS resource from a remote HTTP host.
   * At most one outstanding request may be in-flight,
   * i.e. from the invocation of `fetch()` until either
   * a callback or `cancel()` is invoked, no
   * additional `fetch()` may be issued.
   * the URI to to fetch is to be obtained at construction time
   * for example from RemoteJwks configuration
   *
   * @param parent_span the active span to create children under
   * @param receiver the receiver of the fetched JWKS or error.
   *
   *
   */
  virtual void fetch(Tracing::Span& parent_span, JwksReceiver& receiver) PURE;

  /*
   * Factory method for creating a JwksFetcher.
   * @param cm the cluster manager to use during Jwks retrieval
   * @param remote_jwks the definition of the remote Jwks source
   * @return a JwksFetcher instance
   */
  static JwksFetcherPtr
  create(Upstream::ClusterManager& cm,
         const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks);
};
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
