#pragma once

#include <cstdint>
#include <string>

#include "envoy/grpc/context.h"
#include "envoy/http/header_map.h"

#include "common/common/hash.h"
#include "common/grpc/stat_names.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Grpc {

struct Context::RequestStatNames {
  Stats::StatName service_; // supplies the service name.
  Stats::StatName method_;  // supplies the method name.
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

  Stats::StatName successStatName(bool success) const { return success ? success_ : failure_; }
  Stats::StatName protocolStatName(Protocol protocol) const {
    return protocol == Context::Protocol::Grpc ? grpc_ : grpc_web_;
  }

  StatNames& statNames() override { return stat_names_; }

private:
  // Makes a stat name from a string, if we don't already have one for it.
  // This always takes a lock on mutex_, and if we haven't seen the name
  // before, it also takes a lock on the symbol table.
  //
  // TODO(jmarantz): See https://github.com/envoyproxy/envoy/pull/7008 for
  // a lock-free approach to creating dynamic stat-names based on requests.
  Stats::StatName makeDynamicStatName(absl::string_view name);

  // Gets the stat prefix and underlying storage, depending on whether request_names is empty
  // or not.
  // Prefix will be "<protocol>" if request_names is empty, or
  // "<protocol>.<service>.<method>" if it is not empty.
  std::pair<Stats::StatName, Stats::SymbolTable::StoragePtr>
  getPrefix(Protocol protocol, const absl::optional<RequestStatNames>& request_names);

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
  const Stats::StatName upstream_rq_time_;

  StatNames stat_names_;
};

} // namespace Grpc
} // namespace Envoy
