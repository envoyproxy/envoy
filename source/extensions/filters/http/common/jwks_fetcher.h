#pragma once

#include "envoy/api/v2/core/http_uri.pb.h"
#include "envoy/common/pure.h"
#include "envoy/upstream/cluster_manager.h"

#include "jwt_verify_lib/jwks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

class JwksFetcher;
typedef std::unique_ptr<JwksFetcher> JwksFetcherPtr;
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
      /* A network error occured causing JWKS retrieval failure. */
      Network,
      /* A failure occured when trying to parse the retrieved JWKS data. */
      InvalidJwks,
    };

    virtual ~JwksReceiver(){};
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

  virtual ~JwksFetcher(){};

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
   * @param uri the uri to retrieve the jwks from.
   * @param receiver the receiver of the fetched JWKS or error.
   */
  virtual void fetch(const ::envoy::api::v2::core::HttpUri& uri, JwksReceiver& receiver) PURE;

  /*
   * Factory method for creating a JwksFetcher.
   * @param cm the cluster manager to use during Jwks retrieval
   * @return a JwksFetcher instance
   */
  static JwksFetcherPtr create(Upstream::ClusterManager& cm);
};
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
