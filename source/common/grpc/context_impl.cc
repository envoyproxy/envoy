#include "source/common/grpc/context_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "source/common/config/well_known_names.h"
#include "source/common/grpc/common.h"
#include "source/common/stats/utility.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Grpc {

namespace {

// The service and method of one charge call, as stat names. Symbolic names are passed through;
// dynamic ones are encoded into a caller-provided storage vector.
struct ResolvedRequestNames {
  Stats::StatName service_;
  Stats::StatName method_;
};

using DynamicStorage = absl::InlinedVector<Stats::StatNameDynamicStorage, 2>;

// Resolves one service/method element: a symbolic element is passed through and a dynamic one is
// encoded into storage.
Stats::StatName elementToStatName(const Stats::Element& element, DynamicStorage& storage,
                                  Stats::SymbolTable& symbol_table) {
  if (const Stats::StatName* name = absl::get_if<Stats::StatName>(&element); name != nullptr) {
    return *name;
  }
  if (const Stats::DynamicName* name = absl::get_if<Stats::DynamicName>(&element);
      name != nullptr) {
    return storage.emplace_back(*name, symbol_table).statName();
  }
  return storage.emplace_back(absl::get<Stats::DynamicSavedName>(element), symbol_table).statName();
}

// Resolves the service and method of request_names into stat names, or returns nullopt when
// there are none.
std::optional<ResolvedRequestNames>
resolveRequestNames(const std::optional<Context::RequestStatNames>& request_names,
                    DynamicStorage& storage, Stats::SymbolTable& symbol_table) {
  if (!request_names.has_value()) {
    return std::nullopt;
  }
  return ResolvedRequestNames{elementToStatName(request_names->service_, storage, symbol_table),
                              elementToStatName(request_names->method_, storage, symbol_table)};
}

// Returns the "<protocol>.[<service>.<method>.]<suffix>" stat of the scope. The service and
// method are the envoy.grpc_bridge_service and envoy.grpc_bridge_method tags of the stat and
// leave its tag-extracted name.
Stats::Counter& counter(Stats::Scope& scope, Stats::StatName protocol_name,
                        const std::optional<ResolvedRequestNames>& names, Stats::StatName suffix,
                        Stats::StatName service_tag_name, Stats::StatName method_tag_name,
                        Stats::SymbolTable& symbol_table) {
  if (!names.has_value()) {
    const Stats::StatNameJoiner joiner({protocol_name, suffix}, symbol_table);
    return scope.counterFromStatName(joiner.statName());
  }
  const Stats::StatNameJoiner flat_name({protocol_name, names->service_, names->method_, suffix},
                                        symbol_table);
  const Stats::StatNameJoiner base_name({protocol_name, suffix}, symbol_table);
  const Stats::StatNameTag tags[2]{{service_tag_name, names->service_},
                                   {method_tag_name, names->method_}};
  return scope.counterFromTaggedName(base_name.statName(), tags, flat_name.statName());
}

Stats::Histogram& histogram(Stats::Scope& scope, Stats::StatName protocol_name,
                            const std::optional<ResolvedRequestNames>& names,
                            Stats::StatName suffix, Stats::StatName service_tag_name,
                            Stats::StatName method_tag_name, Stats::SymbolTable& symbol_table) {
  if (!names.has_value()) {
    const Stats::StatNameJoiner joiner({protocol_name, suffix}, symbol_table);
    return scope.histogramFromStatName(joiner.statName(), Stats::Histogram::Unit::Milliseconds);
  }
  const Stats::StatNameJoiner flat_name({protocol_name, names->service_, names->method_, suffix},
                                        symbol_table);
  const Stats::StatNameJoiner base_name({protocol_name, suffix}, symbol_table);
  const Stats::StatNameTag tags[2]{{service_tag_name, names->service_},
                                   {method_tag_name, names->method_}};
  return scope.histogramFromTaggedName(base_name.statName(), tags, flat_name.statName(),
                                       Stats::Histogram::Unit::Milliseconds);
}

} // namespace

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : symbol_table_(symbol_table), stat_name_pool_(symbol_table),
      grpc_(stat_name_pool_.add("grpc")), grpc_web_(stat_name_pool_.add("grpc-web")),
      success_(stat_name_pool_.add("success")), failure_(stat_name_pool_.add("failure")),
      total_(stat_name_pool_.add("total")), zero_(stat_name_pool_.add("0")),
      request_message_count_(stat_name_pool_.add("request_message_count")),
      response_message_count_(stat_name_pool_.add("response_message_count")),
      upstream_rq_time_(stat_name_pool_.add("upstream_rq_time")),
      service_tag_name_(stat_name_pool_.add(Envoy::Config::TagNames::get().GRPC_BRIDGE_SERVICE)),
      method_tag_name_(stat_name_pool_.add(Envoy::Config::TagNames::get().GRPC_BRIDGE_METHOD)),
      stat_names_(symbol_table) {}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                             const std::optional<RequestStatNames>& request_names,
                             const Http::HeaderEntry* grpc_status) {
  if (!grpc_status) {
    return;
  }

  absl::string_view status_str = grpc_status->value().getStringView();
  auto iter = stat_names_.status_names_.find(status_str);
  std::optional<Stats::StatNameDynamicStorage> dynamic_status;
  Stats::StatName status_name;
  if (iter == stat_names_.status_names_.end()) {
    dynamic_status.emplace(status_str, symbol_table_);
    status_name = dynamic_status->statName();
  } else {
    status_name = iter->second;
  }

  DynamicStorage storage;
  const auto names = resolveRequestNames(request_names, storage, symbol_table_);
  counter(cluster.statsScope(), protocolStatName(protocol), names, status_name, service_tag_name_,
          method_tag_name_, symbol_table_)
      .inc();
  chargeStat(cluster, protocol, request_names, (status_str == "0"));
}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                             const std::optional<RequestStatNames>& request_names, bool success) {
  DynamicStorage storage;
  const auto names = resolveRequestNames(request_names, storage, symbol_table_);
  const Stats::StatName protocol_name = protocolStatName(protocol);
  counter(cluster.statsScope(), protocol_name, names, successStatName(success), service_tag_name_,
          method_tag_name_, symbol_table_)
      .inc();
  counter(cluster.statsScope(), protocol_name, names, total_, service_tag_name_, method_tag_name_,
          symbol_table_)
      .inc();
}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster,
                             const std::optional<RequestStatNames>& request_names, bool success) {
  chargeStat(cluster, Protocol::Grpc, request_names, success);
}

void ContextImpl::chargeRequestMessageStat(const Upstream::ClusterInfo& cluster,
                                           const std::optional<RequestStatNames>& request_names,
                                           uint64_t amount) {
  DynamicStorage storage;
  const auto names = resolveRequestNames(request_names, storage, symbol_table_);
  counter(cluster.statsScope(), protocolStatName(Protocol::Grpc), names, request_message_count_,
          service_tag_name_, method_tag_name_, symbol_table_)
      .add(amount);
}

void ContextImpl::chargeResponseMessageStat(const Upstream::ClusterInfo& cluster,
                                            const std::optional<RequestStatNames>& request_names,
                                            uint64_t amount) {
  DynamicStorage storage;
  const auto names = resolveRequestNames(request_names, storage, symbol_table_);
  counter(cluster.statsScope(), protocolStatName(Protocol::Grpc), names, response_message_count_,
          service_tag_name_, method_tag_name_, symbol_table_)
      .add(amount);
}

void ContextImpl::chargeUpstreamStat(const Upstream::ClusterInfo& cluster,
                                     const std::optional<RequestStatNames>& request_names,
                                     std::chrono::milliseconds duration) {
  DynamicStorage storage;
  const auto names = resolveRequestNames(request_names, storage, symbol_table_);
  histogram(cluster.statsScope(), protocolStatName(Protocol::Grpc), names, upstream_rq_time_,
            service_tag_name_, method_tag_name_, symbol_table_)
      .recordValue(duration.count());
}

std::optional<ContextImpl::RequestStatNames>
ContextImpl::resolveDynamicServiceAndMethod(const Http::HeaderEntry* path) {
  std::optional<Common::RequestNames> request_names = Common::resolveServiceAndMethod(path);

  if (!request_names) {
    return {};
  }

  // service/method will live until the request is finished to emit resulting stats (e.g. request
  // status) values in request_names_ might get changed, for example by routing rules (i.e.
  // "prefix_rewrite"), so we copy them here to preserve the initial value and get a proper stat
  Stats::Element service = Stats::DynamicSavedName(request_names->service_);
  Stats::Element method = Stats::DynamicSavedName(request_names->method_);
  return RequestStatNames{service, method};
}

std::optional<ContextImpl::RequestStatNames>
ContextImpl::resolveDynamicServiceAndMethodWithDotReplaced(const Http::HeaderEntry* path) {
  std::optional<Common::RequestNames> request_names = Common::resolveServiceAndMethod(path);
  if (!request_names) {
    return {};
  }
  Stats::Element service =
      Stats::DynamicSavedName(absl::StrReplaceAll(request_names->service_, {{".", "_"}}));
  Stats::Element method = Stats::DynamicSavedName(request_names->method_);
  return RequestStatNames{service, method};
}

} // namespace Grpc
} // namespace Envoy
