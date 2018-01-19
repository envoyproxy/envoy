#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/filter.h"
#include "envoy/tracing/http_tracer.h"

#include "api/auth/external_auth.pb.h"

namespace Envoy {
namespace ExtAuthz {

/**
 * Possible async results for a check call.
 */
enum class CheckStatus {
  // The request is authorized.
  OK,
  // The authz service could not be queried.
  Error,
  // The request is denied.
  Denied
};

/**
 * Async callbacks used during check() calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() {}

  /**
   * Called when a check request is complete. The resulting status is supplied.
   */
  virtual void complete(CheckStatus status) PURE;
};


class Client {
 public:
  // Destructor
  virtual ~Client() {}

  /**
   * Cancel an inflight Check request.
   */
  virtual void cancel() PURE;

  // A check call.
  virtual void check(RequestCallbacks &callback, const envoy::api::v2::auth::CheckRequest& request,
                     Tracing::Span& parent_span) PURE;

};

typedef std::unique_ptr<Client> ClientPtr;

/**
 * An interface for creating a  external authorization client.
 */
class ClientFactory {
public:
  virtual ~ClientFactory() {}

  /**
   * Return a new authz client.
   */
  virtual ClientPtr create(const Optional<std::chrono::milliseconds>& timeout) PURE;
};

typedef std::unique_ptr<ClientFactory> ClientFactoryPtr;


/**
 * An interface for creating ext_authz.proto (authorization) request.
 * CheckRequestGenerator is used to extract attributes from the TCP/HTTP request
 * and fill out the details in the authorization protobuf that is sent to authorization
 * service.
 */
class CheckRequestGenerator {
public:
    // Destructor
  virtual ~CheckRequestGenerator() {}

  virtual void createHttpCheck(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                               const Envoy::Http::HeaderMap &headers,
                               envoy::api::v2::auth::CheckRequest& request) PURE;
  virtual void createTcpCheck(const Network::ReadFilterCallbacks* callbacks,
                              envoy::api::v2::auth::CheckRequest& request) PURE;
};

typedef std::unique_ptr<CheckRequestGenerator> CheckRequestGeneratorPtr;

} // namespace ExtAuthz
} // namespace Envoy

