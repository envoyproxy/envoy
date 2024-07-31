#include "source/extensions/filters/network/generic_proxy/stats.h"

#include "source/common/stream_info/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

constexpr uint32_t MaxCodeValue = 999;

CodeOrFlags::CodeOrFlags(Server::Configuration::ServerFactoryContext& context)
    : pool_(context.scope().symbolTable()) {

  // Only 0-999 are valid status codes for now. This should be enough for all practical purposes.
  // And because this should be global singleton, it's fine to preallocate all of them.
  for (uint32_t i = 0; i <= MaxCodeValue; ++i) {
    code_stat_names_.push_back(pool_.add(std::to_string(i)));
  }

  flag_stat_names_.reserve(StreamInfo::ResponseFlagUtils::responseFlagsVec().size());

  for (const auto& flag : StreamInfo::ResponseFlagUtils::responseFlagsVec()) {
    flag_stat_names_.push_back(pool_.add(flag.short_string_));
  }

  unknown_code_or_flag_ = pool_.add("-");
}

Stats::StatName CodeOrFlags::statNameFromCode(uint32_t code) const {
  if (code <= MaxCodeValue) {
    return code_stat_names_[code];
  }
  return unknown_code_or_flag_;
}

Stats::StatName CodeOrFlags::statNameFromFlag(StreamInfo::ResponseFlag flag) const {
  // Any flag value should be less than the size of flag_stat_names_. Because flag_stat_names_
  // is initialized with all possible flags.
  ASSERT(flag.value() < flag_stat_names_.size());
  return flag_stat_names_[flag.value()];
}

void GenericFilterStatsHelper::onRequestReset() { stats_.downstream_rq_reset_.inc(); }

void GenericFilterStatsHelper::onRequestDecodingError() {
  stats_.downstream_rq_decoding_error_.inc();
}

void GenericFilterStatsHelper::onRequest() {
  stats_.downstream_rq_total_.inc();
  stats_.downstream_rq_active_.inc();
}
void GenericFilterStatsHelper::onRequestComplete(const StreamInfo::StreamInfo& info,
                                                 bool local_reply, bool error_reply) {
  stats_.downstream_rq_active_.dec();

  if (local_reply) {
    stats_.downstream_rq_local_.inc();
  }
  if (error_reply) {
    stats_.downstream_rq_error_.inc();
  }

  // Record request time.
  auto rq_time_ms = info.requestComplete();
  if (rq_time_ms.has_value()) {
    stats_.downstream_rq_time_.recordValue(
        std::chrono::duration_cast<std::chrono::milliseconds>(rq_time_ms.value()).count());
  }

  // Record request tx time.
  StreamInfo::TimingUtility timing(info);
  auto rq_tx_time_us = timing.lastUpstreamTxByteSent();
  if (rq_tx_time_us.has_value()) {
    stats_.downstream_rq_tx_time_.recordValue(
        std::chrono::duration_cast<std::chrono::microseconds>(rq_tx_time_us.value()).count());
  }

  const auto response_code = info.responseCode().value_or(0);
  const auto response_flags = info.responseFlags();

  if (last_code_counter_.second.has_value() && response_code == last_code_counter_.first) {
    last_code_counter_.second->inc();
  } else {
    auto name_storage =
        stats_scope_.symbolTable().join({stats_.stats_prefix_, stats_.downstream_rq_code_,
                                         code_or_flag_.statNameFromCode(response_code)});
    last_code_counter_ = {response_code,
                          stats_scope_.counterFromStatName(Stats::StatName{name_storage.get()})};
    last_code_counter_.second->inc();
  }

  for (const auto& flag : response_flags) {
    if (last_flag_counter_.second.has_value() && flag == last_flag_counter_.first) {
      last_flag_counter_.second->inc();
    } else {
      auto name_storage = stats_scope_.symbolTable().join(
          {stats_.stats_prefix_, stats_.downstream_rq_flag_, code_or_flag_.statNameFromFlag(flag)});

      last_flag_counter_ = {flag,
                            stats_scope_.counterFromStatName(Stats::StatName{name_storage.get()})};
      last_flag_counter_.second->inc();
    }
  }
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
