#include "common/grpc/context_impl.h"

#include <cstdint>
#include <string>

#include "common/grpc/common.h"

namespace Envoy {
namespace Grpc {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : symbol_table_(symbol_table), stat_name_pool_(symbol_table),
      grpc_(stat_name_pool_.add("grpc")), grpc_web_(stat_name_pool_.add("grpc-web")),
      success_(stat_name_pool_.add("success")), failure_(stat_name_pool_.add("failure")),
      total_(stat_name_pool_.add("total")), zero_(stat_name_pool_.add("0")),
      request_message_count_(stat_name_pool_.add("request_message_count")),
      response_message_count_(stat_name_pool_.add("response_message_count")),
      stat_names_(symbol_table) {}

// Makes a stat name from a string, if we don't already have one for it.
// This always takes a lock on mutex_, and if we haven't seen the name
// before, it also takes a lock on the symbol table.
//
// TODO(jmarantz): See https://github.com/envoyproxy/envoy/pull/7008 for
// a lock-free approach to creating dynamic stat-names based on requests.
Stats::StatName ContextImpl::makeDynamicStatName(absl::string_view name) {
  Thread::LockGuard lock(mutex_);
  auto iter = stat_name_map_.find(name);
  if (iter != stat_name_map_.end()) {
    return iter->second;
  }
  const Stats::StatName stat_name = stat_name_pool_.add(name);
  stat_name_map_[std::string(name)] = stat_name;
  return stat_name;
}

// Gets the stat prefix and underlying storage, depending on whether request_names is empty
std::pair<Stats::StatName, Stats::SymbolTable::StoragePtr>
ContextImpl::getPrefix(Protocol protocol, const absl::optional<RequestStatNames>& request_names) {
  const Stats::StatName protocolName = protocolStatName(protocol);
  if (request_names) {
    Stats::SymbolTable::StoragePtr prefix_storage =
        symbol_table_.join({protocolName, request_names->service_, request_names->method_});
    Stats::StatName prefix = Stats::StatName(prefix_storage.get());
    return {prefix, std::move(prefix_storage)};
  } else {
    return {protocolName, nullptr};
  }
}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                             const absl::optional<RequestStatNames>& request_names,
                             const Http::HeaderEntry* grpc_status) {
  if (!grpc_status) {
    return;
  }

  absl::string_view status_str = grpc_status->value().getStringView();
  auto iter = stat_names_.status_names_.find(status_str);
  const Stats::StatName status_stat_name =
      (iter != stat_names_.status_names_.end()) ? iter->second : makeDynamicStatName(status_str);
  const Stats::SymbolTable::StoragePtr stat_name_storage =
      request_names ? symbol_table_.join({protocolStatName(protocol), request_names->service_,
                                          request_names->method_, status_stat_name})
                    : symbol_table_.join({protocolStatName(protocol), status_stat_name});

  cluster.statsScope().counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
  chargeStat(cluster, protocol, request_names, (status_str == "0"));
}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                             const absl::optional<RequestStatNames>& request_names, bool success) {
  auto prefix_and_storage = getPrefix(protocol, request_names);
  Stats::StatName prefix = prefix_and_storage.first;

  const Stats::SymbolTable::StoragePtr status =
      symbol_table_.join({prefix, successStatName(success)});
  const Stats::SymbolTable::StoragePtr total = symbol_table_.join({prefix, total_});

  cluster.statsScope().counterFromStatName(Stats::StatName(status.get())).inc();
  cluster.statsScope().counterFromStatName(Stats::StatName(total.get())).inc();
}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster,
                             const absl::optional<RequestStatNames>& request_names, bool success) {
  chargeStat(cluster, Protocol::Grpc, request_names, success);
}

void ContextImpl::chargeRequestMessageStat(const Upstream::ClusterInfo& cluster,
                                           const absl::optional<RequestStatNames>& request_names,
                                           uint64_t amount) {
  auto prefix_and_storage = getPrefix(Protocol::Grpc, request_names);
  Stats::StatName prefix = prefix_and_storage.first;

  const Stats::SymbolTable::StoragePtr request_message_count =
      symbol_table_.join({prefix, request_message_count_});

  cluster.statsScope()
      .counterFromStatName(Stats::StatName(request_message_count.get()))
      .add(amount);
}

void ContextImpl::chargeResponseMessageStat(const Upstream::ClusterInfo& cluster,
                                            const absl::optional<RequestStatNames>& request_names,
                                            uint64_t amount) {
  auto prefix_and_storage = getPrefix(Protocol::Grpc, request_names);
  Stats::StatName prefix = prefix_and_storage.first;

  const Stats::SymbolTable::StoragePtr response_message_count =
      symbol_table_.join({prefix, response_message_count_});

  cluster.statsScope()
      .counterFromStatName(Stats::StatName(response_message_count.get()))
      .add(amount);
}

absl::optional<ContextImpl::RequestStatNames>
ContextImpl::resolveDynamicServiceAndMethod(const Http::HeaderEntry* path) {
  absl::optional<Common::RequestNames> request_names = Common::resolveServiceAndMethod(path);
  if (!request_names) {
    return {};
  }

  const Stats::StatName service = makeDynamicStatName(request_names->service_);
  const Stats::StatName method = makeDynamicStatName(request_names->method_);
  return RequestStatNames{service, method};
}

} // namespace Grpc
} // namespace Envoy
