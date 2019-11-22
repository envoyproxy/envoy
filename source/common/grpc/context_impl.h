#pragma once

#include <cstdint>
#include <string>

#include "envoy/grpc/context.h"
#include "envoy/http/header_map.h"

#include "common/common/hash.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Grpc {

struct Context::RequestNames {
  Stats::StatName service_; // supplies the service name.
  Stats::StatName method_;  // supplies the method name.
};

class ContextImpl : public Context {
public:
  explicit ContextImpl(Stats::SymbolTable& symbol_table);

  // Context
  void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                  const RequestNames& request_names, const Http::HeaderEntry* grpc_status) override;
  void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                  const RequestNames& request_names, bool success) override;
  void chargeStat(const Upstream::ClusterInfo& cluster, const RequestNames& request_names,
                  bool success) override;
  void chargeRequestMessageStat(const Upstream::ClusterInfo& cluster,
                                const RequestNames& request_names, uint64_t amount) override;
  void chargeResponseMessageStat(const Upstream::ClusterInfo& cluster,
                                 const RequestNames& request_names, uint64_t amount) override;

  /**
   * Resolve the gRPC service and method from the HTTP2 :path header.
   * @param path supplies the :path header.
   * @param service supplies the output pointer of the gRPC service.
   * @param method supplies the output pointer of the gRPC method.
   * @return bool true if both gRPC serve and method have been resolved successfully.
   */
  absl::optional<RequestNames> resolveServiceAndMethod(const Http::HeaderEntry* path) override;

  Stats::StatName successStatName(bool success) const { return success ? success_ : failure_; }
  Stats::StatName protocolStatName(Protocol protocol) const {
    return protocol == Context::Protocol::Grpc ? grpc_ : grpc_web_;
  }

private:
  // Makes a stat name from a string, if we don't already have one for it.
  // This always takes a lock on mutex_, and if we haven't seen the name
  // before, it also takes a lock on the symbol table.
  //
  // TODO(jmarantz): See https://github.com/envoyproxy/envoy/pull/7008 for
  // a lock-free approach to creating dynamic stat-names based on requests.
  Stats::StatName makeDynamicStatName(absl::string_view name);

  Stats::SymbolTable& symbol_table_;
  mutable Thread::MutexBasicLockable mutex_;
  Stats::StatNamePool stat_name_pool_ ABSL_GUARDED_BY(mutex_);
  StringMap<Stats::StatName> stat_name_map_ ABSL_GUARDED_BY(mutex_);
  const Stats::StatName grpc_;
  const Stats::StatName grpc_web_;
  const Stats::StatName success_;
  const Stats::StatName failure_;
  const Stats::StatName total_;
  const Stats::StatName zero_;
  const Stats::StatName request_message_count_;
  const Stats::StatName response_message_count_;
};

} // namespace Grpc
} // namespace Envoy
