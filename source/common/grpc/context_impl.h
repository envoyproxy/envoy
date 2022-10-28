#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/grpc/context.h"
#include "envoy/http/header_map.h"

#include "source/common/common/hash.h"
#include "source/common/grpc/stat_names.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Grpc {

struct Context::RequestStatNames {
  Stats::Element service_; // supplies the service name.
  Stats::Element method_;  // supplies the method name.
};

class ContextImpl : public Context {
public:
  explicit ContextImpl(Stats::SymbolTable& symbol_table);

  // Context
  void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                  const absl::optional<RequestStatNames>& request_names,
                  const Http::HeaderEntry* grpc_status) override;
  void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                  const absl::optional<RequestStatNames>& request_names, bool success) override;
  void chargeStat(const Upstream::ClusterInfo& cluster,
                  const absl::optional<RequestStatNames>& request_names, bool success) override;
  void chargeRequestMessageStat(const Upstream::ClusterInfo& cluster,
                                const absl::optional<RequestStatNames>& request_names,
                                uint64_t amount) override;
  void chargeResponseMessageStat(const Upstream::ClusterInfo& cluster,
                                 const absl::optional<RequestStatNames>& request_names,
                                 uint64_t amount) override;
  void chargeUpstreamStat(const Upstream::ClusterInfo& cluster,
                          const absl::optional<RequestStatNames>& request_names,
                          std::chrono::milliseconds duration) override;

  /**
   * Resolve the gRPC service and method from the HTTP2 :path header.
   * @param path supplies the :path header.
   * @return if both gRPC serve and method have been resolved successfully returns
   *   a populated RequestStatNames, otherwise returns an empty optional.
   */
  absl::optional<RequestStatNames>
  resolveDynamicServiceAndMethod(const Http::HeaderEntry* path) override;

  /**
   * Resolve the gRPC service and method from the HTTP2 :path header. Replace dots in the gRPC
   * service name if there are any.
   * @param path supplies the :path header.
   * @return if both gRPC serve and method have been resolved successfully returns
   *   a populated RequestStatNames, otherwise returns an empty optional.
   */
  absl::optional<RequestStatNames>
  resolveDynamicServiceAndMethodWithDotReplaced(const Http::HeaderEntry* path) override;

  Stats::StatName successStatName(bool success) const { return success ? success_ : failure_; }
  Stats::StatName protocolStatName(Protocol protocol) const {
    return protocol == Context::Protocol::Grpc ? grpc_ : grpc_web_;
  }

  StatNames& statNames() override { return stat_names_; }

private:
  // Creates an array of stat-name elements, comprising the protocol, optional
  // service and method, and a suffix.
  Stats::ElementVec statElements(Protocol protocol,
                                 const absl::optional<RequestStatNames>& request_names,
                                 Stats::Element suffix);

  Stats::StatNamePool stat_name_pool_;
  const Stats::StatName grpc_;
  const Stats::StatName grpc_web_;
  const Stats::StatName success_;
  const Stats::StatName failure_;
  const Stats::StatName total_;
  const Stats::StatName zero_;
  const Stats::StatName request_message_count_;
  const Stats::StatName response_message_count_;
  const Stats::StatName upstream_rq_time_;

  StatNames stat_names_;
};

} // namespace Grpc
} // namespace Envoy
