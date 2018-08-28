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
 * retrieve a single JWKS and should not be re-used to fetch further instances.
 */
class JwksFetcher {
public:
  class JwksReceiver {
  public:
    enum class Failure {
      Unknown,
      Network,
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
   * Cancel any inflight request.
   */
  virtual void cancel() PURE;

  /*
   * Retrieve a JWKS resource from a remote HTTP host.
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
