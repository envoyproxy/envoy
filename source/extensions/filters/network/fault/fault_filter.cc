#include "source/extensions/filters/network/fault/fault_filter.h"

#include <string>

#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Fault {

namespace {

struct RcDetailsValues {
  // The fault filter injected an abort for this connection.
  const std::string FaultAbort = "fault_filter_abort";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

} // namespace

FaultFilterConfig::FaultFilterConfig(
    const envoy::extensions::filters::network::fault::v3::NetworkFault& proto_config,
    Stats::Scope& scope, Runtime::Loader& runtime)
    : has_abort_(proto_config.has_abort()), abort_percentage_(proto_config.abort().percentage()),
      dry_run_(proto_config.abort().dry_run()),
      abort_percent_runtime_(PROTOBUF_GET_STRING_OR_DEFAULT(proto_config, abort_percent_runtime,
                                                            RuntimeKeys::get().AbortPercentKey)),
      has_filter_metadata_(proto_config.has_filter_metadata()),
      filter_metadata_(proto_config.filter_metadata()), runtime_(runtime),
      stats_(generateStats(proto_config.stat_prefix(), scope)) {}

FaultFilterStats FaultFilterConfig::generateStats(absl::string_view stat_prefix,
                                                  Stats::Scope& scope) {
  const absl::string_view trimmed = absl::StripSuffix(stat_prefix, ".");
  const std::string final_prefix =
      trimmed.empty() ? "fault." : absl::StrCat("fault.", trimmed, ".");
  return {ALL_NETWORK_FAULT_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

Network::FilterStatus FaultFilter::onNewConnection() {
  ASSERT(read_callbacks_ != nullptr);
  if (evaluated_) {
    return aborted_ ? Network::FilterStatus::StopIteration : Network::FilterStatus::Continue;
  }
  evaluated_ = true;

  if (!isAbortEnabled()) {
    return Network::FilterStatus::Continue;
  }

  if (config_->isAbortDryRun()) {
    recordAbortDryRun();
    return Network::FilterStatus::Continue;
  }

  aborted_ = true;
  abortConnection();
  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus FaultFilter::onData(Buffer::Instance&, bool) {
  return aborted_ ? Network::FilterStatus::StopIteration : Network::FilterStatus::Continue;
}

bool FaultFilter::isAbortEnabled() const {
  if (!config_->hasAbort()) {
    return false;
  }
  return config_->runtime().snapshot().featureEnabled(config_->abortPercentRuntime(),
                                                      config_->abortPercentage());
}

void FaultFilter::recordDynamicMetadata() {
  if (config_->hasFilterMetadata()) {
    read_callbacks_->connection().streamInfo().setDynamicMetadata(NetworkFilterNames::get().Fault,
                                                                  config_->filterMetadata());
  }
}

void FaultFilter::recordAbortDryRun() {
  ENVOY_CONN_LOG(debug, "network fault: dry-run abort matched", read_callbacks_->connection());
  config_->stats().aborts_dry_run_.inc();
  recordDynamicMetadata();
}

void FaultFilter::abortConnection() {
  ENVOY_CONN_LOG(debug, "network fault: aborting connection", read_callbacks_->connection());
  config_->stats().aborts_injected_.inc();
  auto& stream_info = read_callbacks_->connection().streamInfo();
  stream_info.setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected);
  stream_info.setResponseCodeDetails(RcDetails::get().FaultAbort);
  stream_info.setConnectionTerminationDetails(RcDetails::get().FaultAbort);
  recordDynamicMetadata();
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush,
                                      RcDetails::get().FaultAbort);
}

} // namespace Fault
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
