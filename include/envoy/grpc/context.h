#pragma once

#include <memory>

#include "envoy/http/header_map.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Grpc {

/**
 * Captures http-related structures with cardinality of one per server.
 */
class Context {
public:
  virtual ~Context() = default;

  enum class Protocol { Grpc, GrpcWeb };

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use.
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param grpc_status supplies the gRPC status.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                          const std::string& grpc_service, const std::string& grpc_method,
                          const Http::HeaderEntry* grpc_status) PURE;

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use.
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param success supplies whether the call succeeded.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                          const std::string& grpc_service, const std::string& grpc_method, 
                          bool success) PURE;

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param success supplies whether the call succeeded.
   */
  virtual void chargeStat(const Upstream::ClusterInfo& cluster, const std::string& grpc_service,
                          const std::string& grpc_method, bool success) PURE;;
};

using ContextPtr = std::unique_ptr<Context>;

} // namespace Http
} // namespace Envoy
