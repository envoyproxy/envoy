#pragma once

#include <memory>

#include "envoy/http/header_map.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Grpc {

struct StatNames;

/**
 * Captures grpc-related structures with cardinality of one per server.
 */
class Context {
public:
  virtual ~Context() = default;

  enum class Protocol { Grpc, GrpcWeb };

  struct RequestStatNames;

  /**
   * Parses out request grpc service-name and method from the path, returning a
   * populated RequestStatNames if successful. See the implementation
   * (source/common/grpc/common.h) for the definition of RequestStatNames. It is
   * hidden in the implementation since it references StatName, which is defined
   * only in the stats implementation.
   *
   * @param path the request path.
   * @return the request names, expressed as StatName.
   */
  virtual absl::optional<RequestStatNames>
  resolveDynamicServiceAndMethod(const Http::HeaderEntry* path) PURE;

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use.
   * @param request_names supplies the request names.
   * @param grpc_status supplies the gRPC status.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                          const absl::optional<RequestStatNames>& request_names,
                          const Http::HeaderEntry* grpc_status) PURE;

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use.
   * @param request_names supplies the request names.
   * @param success supplies whether the call succeeded.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                          const absl::optional<RequestStatNames>& request_names, bool success) PURE;

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param request_names supplies the request names.
   * @param success supplies whether the call succeeded.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster,
                          const absl::optional<RequestStatNames>& request_names, bool success) PURE;

  /**
   * Charge a request message stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param request_names supplies the request names.
   * @param amount supplies the number of the request messages.
   */
  virtual void chargeRequestMessageStat(const Upstream::ClusterInfo& cluster,
                                        const absl::optional<RequestStatNames>& request_names,
                                        uint64_t amount) PURE;

  /**
   * Charge a response message stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param request_names supplies the request names.
   * @param amount supplies the number of the response messages.
   */
  virtual void chargeResponseMessageStat(const Upstream::ClusterInfo& cluster,
                                         const absl::optional<RequestStatNames>& request_names,
                                         uint64_t amount) PURE;

  /**
   * Charge upstream stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param request_names supplies the request names.
   * @param duration supplies the duration of the upstream request.
   */
  virtual void chargeUpstreamStat(const Upstream::ClusterInfo& cluster,
                                  const absl::optional<RequestStatNames>& request_names,
                                  std::chrono::milliseconds duration) PURE;

  /**
   * @return a struct containing StatNames for gRPC stat tokens.
   */
  virtual StatNames& statNames() PURE;
};

using ContextPtr = std::unique_ptr<Context>;

} // namespace Grpc
} // namespace Envoy
