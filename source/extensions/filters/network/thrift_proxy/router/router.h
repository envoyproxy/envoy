#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/router/router.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_converter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RateLimitPolicy;

/**
 * RequestMirrorPolicy is an individual mirroring rule for a route entry.
 */
class RequestMirrorPolicy {
public:
  virtual ~RequestMirrorPolicy() = default;

  /**
   * @return const std::string& the upstream cluster that should be used for the mirrored request.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * @return bool whether this policy is currently enabled.
   */
  virtual bool enabled(Runtime::Loader& runtime) const PURE;
};

/**
 * ShadowRequestHandle is used to write a request or release a connection early if needed.
 */
class ShadowRequestHandle : public ProtocolConverter {
public:
  ~ShadowRequestHandle() override = default;

  /**
   * Submits a serialized request to be shadowed.
   */
  virtual void tryWriteRequest() PURE;

  /**
   * Releases the upstream connection obtained for the shadow request if there's no request in
   * progress.
   */
  virtual void tryReleaseConnection() PURE;

  /**
   * Checks if the request is currently waiting for an upstream connection to become available.
   */
  virtual bool waitingForConnection() const PURE;
};

/**
 * ShadowWriter is used for submitting requests and ignoring the response.
 */
class ShadowWriter {
public:
  virtual ~ShadowWriter() = default;

  /**
   * Starts the shadow request by requesting an upstream connection.
   */
  virtual absl::optional<std::reference_wrapper<ShadowRequestHandle>>
  submit(const std::string& cluster_name, MessageMetadataSharedPtr metadata,
         TransportType original_transport, ProtocolType original_protocol) PURE;
};

/**
 * RouteEntry is an individual resolved route entry.
 */
class RouteEntry {
public:
  virtual ~RouteEntry() = default;

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * @return MetadataMatchCriteria* the metadata that a subset load balancer should match when
   * selecting an upstream host
   */
  virtual const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const PURE;

  /**
   * @return const RateLimitPolicy& the rate limit policy for the route.
   */
  virtual const RateLimitPolicy& rateLimitPolicy() const PURE;

  /**
   * @return bool should the service name prefix be stripped from the method.
   */
  virtual bool stripServiceName() const PURE;

  /**
   * @return const Http::LowerCaseString& the header used to determine the cluster.
   */
  virtual const Http::LowerCaseString& clusterHeader() const PURE;

  /**
   * @return const std::vector<RequestMirrorPolicy>& the mirror policies associated with this route,
   * if any.
   */
  virtual const std::vector<std::shared_ptr<RequestMirrorPolicy>>&
  requestMirrorPolicies() const PURE;
};

/**
 * Route holds the RouteEntry for a request.
 */
class Route {
public:
  virtual ~Route() = default;

  /**
   * @return the route entry or nullptr if there is no matching route for the request.
   */
  virtual const RouteEntry* routeEntry() const PURE;
};

using RouteConstSharedPtr = std::shared_ptr<const Route>;

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() = default;

  /**
   * Based on the incoming Thrift request transport and/or protocol data, determine the target
   * route for the request.
   * @param metadata MessageMetadata for the message to route
   * @param random_value uint64_t used to select cluster affinity
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const PURE;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
