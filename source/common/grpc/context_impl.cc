#include "source/common/grpc/context_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "source/common/grpc/common.h"
#include "source/common/stats/utility.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Grpc {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : stat_name_pool_(symbol_table), grpc_(stat_name_pool_.add("grpc")),
      grpc_web_(stat_name_pool_.add("grpc-web")), success_(stat_name_pool_.add("success")),
      failure_(stat_name_pool_.add("failure")), total_(stat_name_pool_.add("total")),
      zero_(stat_name_pool_.add("0")),
      request_message_count_(stat_name_pool_.add("request_message_count")),
      response_message_count_(stat_name_pool_.add("response_message_count")),
      upstream_rq_time_(stat_name_pool_.add("upstream_rq_time")), stat_names_(symbol_table) {}

// Gets the stat prefix and underlying storage, depending on whether request_names is empty
Stats::ElementVec ContextImpl::statElements(Protocol protocol,
                                            const absl::optional<RequestStatNames>& request_names,
                                            Stats::Element suffix) {
  const Stats::StatName protocolName = protocolStatName(protocol);
  if (request_names) {
    return Stats::ElementVec{protocolName, request_names->service_, request_names->method_, suffix};
  }
  return Stats::ElementVec{protocolName, suffix};
}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                             const absl::optional<RequestStatNames>& request_names,
                             const Http::HeaderEntry* grpc_status) {
  if (!grpc_status) {
    return;
  }

  absl::string_view status_str = grpc_status->value().getStringView();
  auto iter = stat_names_.status_names_.find(status_str);
  Stats::ElementVec elements =
      statElements(protocol, request_names,
                   (iter != stat_names_.status_names_.end()) ? Stats::Element(iter->second)
                                                             : Stats::DynamicName(status_str));
  Stats::Utility::counterFromElements(cluster.statsScope(), elements).inc();
  chargeStat(cluster, protocol, request_names, (status_str == "0"));
}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                             const absl::optional<RequestStatNames>& request_names, bool success) {
  Stats::ElementVec elements = statElements(protocol, request_names, successStatName(success));
  Stats::Utility::counterFromElements(cluster.statsScope(), elements).inc();
  elements.back() = total_;
  Stats::Utility::counterFromElements(cluster.statsScope(), elements).inc();
}

void ContextImpl::chargeStat(const Upstream::ClusterInfo& cluster,
                             const absl::optional<RequestStatNames>& request_names, bool success) {
  chargeStat(cluster, Protocol::Grpc, request_names, success);
}

void ContextImpl::chargeRequestMessageStat(const Upstream::ClusterInfo& cluster,
                                           const absl::optional<RequestStatNames>& request_names,
                                           uint64_t amount) {
  Stats::ElementVec elements = statElements(Protocol::Grpc, request_names, request_message_count_);
  Stats::Utility::counterFromElements(cluster.statsScope(), elements).add(amount);
}

void ContextImpl::chargeResponseMessageStat(const Upstream::ClusterInfo& cluster,
                                            const absl::optional<RequestStatNames>& request_names,
                                            uint64_t amount) {
  Stats::ElementVec elements = statElements(Protocol::Grpc, request_names, response_message_count_);
  Stats::Utility::counterFromElements(cluster.statsScope(), elements).add(amount);
}

void ContextImpl::chargeUpstreamStat(const Upstream::ClusterInfo& cluster,
                                     const absl::optional<RequestStatNames>& request_names,
                                     std::chrono::milliseconds duration) {
  Stats::ElementVec elements = statElements(Protocol::Grpc, request_names, upstream_rq_time_);
  Stats::Utility::histogramFromElements(cluster.statsScope(), elements,
                                        Stats::Histogram::Unit::Milliseconds)
      .recordValue(duration.count());
}

absl::optional<ContextImpl::RequestStatNames>
ContextImpl::resolveDynamicServiceAndMethod(const Http::HeaderEntry* path) {
  absl::optional<Common::RequestNames> request_names = Common::resolveServiceAndMethod(path);

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

absl::optional<ContextImpl::RequestStatNames>
ContextImpl::resolveDynamicServiceAndMethodWithDotReplaced(const Http::HeaderEntry* path) {
  absl::optional<Common::RequestNames> request_names = Common::resolveServiceAndMethod(path);
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
