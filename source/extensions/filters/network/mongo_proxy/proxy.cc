#include "extensions/filters/network/mongo_proxy/proxy.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"

#include "extensions/filters/network/mongo_proxy/codec_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

AccessLog::AccessLog(const std::string& file_name,
                     Envoy::AccessLog::AccessLogManager& log_manager) {
  file_ = log_manager.createAccessLog(file_name);
}

void AccessLog::logMessage(const Message& message, bool full,
                           const Upstream::HostDescription* upstream_host) {
  static const std::string log_format =
      "{{\"time\": \"{}\", \"message\": {}, \"upstream_host\": \"{}\"}}\n";

  SystemTime now = std::chrono::system_clock::now();
  std::string log_line =
      fmt::format(log_format, AccessLogDateTimeFormatter::fromTime(now), message.toString(full),
                  upstream_host ? upstream_host->address()->asString() : "-");

  file_->write(log_line);
}

ProxyFilter::ProxyFilter(const std::string& stat_prefix, Stats::Scope& scope,
                         Runtime::Loader& runtime, AccessLogSharedPtr access_log,
                         const FaultConfigSharedPtr& fault_config,
                         const Network::DrainDecision& drain_decision)
    : stat_prefix_(stat_prefix), scope_(scope), stats_(generateStats(stat_prefix, scope)),
      runtime_(runtime), drain_decision_(drain_decision), access_log_(access_log),
      fault_config_(fault_config) {
  if (!runtime_.snapshot().featureEnabled(MongoRuntimeConfig::get().ConnectionLoggingEnabled,
                                          100)) {
    // If we are not logging at the connection level, just release the shared pointer so that we
    // don't ever log.
    access_log_.reset();
  }
}

ProxyFilter::~ProxyFilter() { ASSERT(!delay_timer_); }

void ProxyFilter::decodeGetMore(GetMoreMessagePtr&& message) {
  tryInjectDelay();

  stats_.op_get_more_.inc();
  logMessage(*message, true);
  ENVOY_LOG(debug, "decoded GET_MORE: {}", message->toString(true));
}

void ProxyFilter::decodeInsert(InsertMessagePtr&& message) {
  tryInjectDelay();

  stats_.op_insert_.inc();
  logMessage(*message, true);
  ENVOY_LOG(debug, "decoded INSERT: {}", message->toString(true));
}

void ProxyFilter::decodeKillCursors(KillCursorsMessagePtr&& message) {
  tryInjectDelay();

  stats_.op_kill_cursors_.inc();
  logMessage(*message, true);
  ENVOY_LOG(debug, "decoded KILL_CURSORS: {}", message->toString(true));
}

void ProxyFilter::decodeQuery(QueryMessagePtr&& message) {
  tryInjectDelay();

  stats_.op_query_.inc();
  logMessage(*message, true);
  ENVOY_LOG(debug, "decoded QUERY: {}", message->toString(true));

  if (message->flags() & QueryMessage::Flags::TailableCursor) {
    stats_.op_query_tailable_cursor_.inc();
  }
  if (message->flags() & QueryMessage::Flags::NoCursorTimeout) {
    stats_.op_query_no_cursor_timeout_.inc();
  }
  if (message->flags() & QueryMessage::Flags::AwaitData) {
    stats_.op_query_await_data_.inc();
  }
  if (message->flags() & QueryMessage::Flags::Exhaust) {
    stats_.op_query_exhaust_.inc();
  }

  ActiveQueryPtr active_query(new ActiveQuery(*this, *message));
  if (!active_query->query_info_.command().empty()) {
    // First field key is the operation.
    scope_.counter(fmt::format("{}cmd.{}.total", stat_prefix_, active_query->query_info_.command()))
        .inc();
  } else {
    // Normal query, get stats on a per collection basis first.
    std::string collection_stat_prefix =
        fmt::format("{}collection.{}", stat_prefix_, active_query->query_info_.collection());
    QueryMessageInfo::QueryType query_type = active_query->query_info_.type();
    chargeQueryStats(collection_stat_prefix, query_type);

    // Callsite stats if we have it.
    if (!active_query->query_info_.callsite().empty()) {
      std::string callsite_stat_prefix =
          fmt::format("{}collection.{}.callsite.{}", stat_prefix_,
                      active_query->query_info_.collection(), active_query->query_info_.callsite());
      chargeQueryStats(callsite_stat_prefix, query_type);
    }

    // Global stats.
    if (active_query->query_info_.max_time() < 1) {
      stats_.op_query_no_max_time_.inc();
    }
    if (query_type == QueryMessageInfo::QueryType::ScatterGet) {
      stats_.op_query_scatter_get_.inc();
    } else if (query_type == QueryMessageInfo::QueryType::MultiGet) {
      stats_.op_query_multi_get_.inc();
    }
  }

  active_query_list_.emplace_back(std::move(active_query));
}

void ProxyFilter::chargeQueryStats(const std::string& prefix,
                                   QueryMessageInfo::QueryType query_type) {
  scope_.counter(fmt::format("{}.query.total", prefix)).inc();
  if (query_type == QueryMessageInfo::QueryType::ScatterGet) {
    scope_.counter(fmt::format("{}.query.scatter_get", prefix)).inc();
  } else if (query_type == QueryMessageInfo::QueryType::MultiGet) {
    scope_.counter(fmt::format("{}.query.multi_get", prefix)).inc();
  }
}

void ProxyFilter::decodeReply(ReplyMessagePtr&& message) {
  stats_.op_reply_.inc();
  logMessage(*message, false);
  ENVOY_LOG(debug, "decoded REPLY: {}", message->toString(true));

  if (message->cursorId() != 0) {
    stats_.op_reply_valid_cursor_.inc();
  }
  if (message->flags() & ReplyMessage::Flags::CursorNotFound) {
    stats_.op_reply_cursor_not_found_.inc();
  }
  if (message->flags() & ReplyMessage::Flags::QueryFailure) {
    stats_.op_reply_query_failure_.inc();
  }

  for (auto i = active_query_list_.begin(); i != active_query_list_.end(); i++) {
    ActiveQuery& active_query = **i;
    if (active_query.query_info_.requestId() != message->responseTo()) {
      continue;
    }

    if (!active_query.query_info_.command().empty()) {
      std::string stat_prefix =
          fmt::format("{}cmd.{}", stat_prefix_, active_query.query_info_.command());
      chargeReplyStats(active_query, stat_prefix, *message);
    } else {
      // Collection stats first.
      std::string stat_prefix =
          fmt::format("{}collection.{}.query", stat_prefix_, active_query.query_info_.collection());
      chargeReplyStats(active_query, stat_prefix, *message);

      // Callsite stats if we have it.
      if (!active_query.query_info_.callsite().empty()) {
        std::string callsite_stat_prefix =
            fmt::format("{}collection.{}.callsite.{}.query", stat_prefix_,
                        active_query.query_info_.collection(), active_query.query_info_.callsite());
        chargeReplyStats(active_query, callsite_stat_prefix, *message);
      }
    }

    active_query_list_.erase(i);
    break;
  }

  if (active_query_list_.empty() && drain_decision_.drainClose() &&
      runtime_.snapshot().featureEnabled(MongoRuntimeConfig::get().DrainCloseEnabled, 100)) {
    ENVOY_LOG(debug, "drain closing mongo connection");
    stats_.cx_drain_close_.inc();

    // We are currently in the write path, so we need to let the write flush out before we close.
    // We do this by creating a timer and firing it with a zero timeout. This will cause it to run
    // in the next event loop iteration. This is really a hack. A better solution would be to
    // introduce the concept of a write complete callback so we can get notified when the write goes
    // out (e.g., flow control, further filters, etc.). This is a much larger project so we can
    // start with this since it will get the job done.
    // TODO(mattklein123): Investigate a better solution for write complete callbacks.
    if (drain_close_timer_ == nullptr) {
      drain_close_timer_ =
          read_callbacks_->connection().dispatcher().createTimer([this] { onDrainClose(); });
      drain_close_timer_->enableTimer(std::chrono::milliseconds(0));
    }
  }
}

void ProxyFilter::decodeCommand(CommandMessagePtr&& message) {
  tryInjectDelay();

  stats_.op_command_.inc();
  logMessage(*message, true);
  ENVOY_LOG(debug, "decoded COMMAND: {}", message->toString(true));
}

void ProxyFilter::decodeCommandReply(CommandReplyMessagePtr&& message) {
  tryInjectDelay();

  stats_.op_command_reply_.inc();
  logMessage(*message, true);
  ENVOY_LOG(debug, "decoded COMMANDREPLY: {}", message->toString(true));
}

void ProxyFilter::onDrainClose() {
  read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
}

void ProxyFilter::chargeReplyStats(ActiveQuery& active_query, const std::string& prefix,
                                   const ReplyMessage& message) {
  uint64_t reply_documents_byte_size = 0;
  for (const Bson::DocumentSharedPtr& document : message.documents()) {
    reply_documents_byte_size += document->byteSize();
  }

  scope_.histogram(fmt::format("{}.reply_num_docs", prefix))
      .recordValue(message.documents().size());
  scope_.histogram(fmt::format("{}.reply_size", prefix)).recordValue(reply_documents_byte_size);
  scope_.histogram(fmt::format("{}.reply_time_ms", prefix))
      .recordValue(std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - active_query.start_time_)
                       .count());
}

void ProxyFilter::doDecode(Buffer::Instance& buffer) {
  if (!sniffing_ ||
      !runtime_.snapshot().featureEnabled(MongoRuntimeConfig::get().ProxyEnabled, 100)) {
    // Safety measure just to make sure that if we have a decoding error we keep going and lose
    // stats. This can be removed once we are more confident of this code.
    buffer.drain(buffer.length());
    return;
  }

  if (!decoder_) {
    decoder_ = createDecoder(*this);
  }

  try {
    decoder_->onData(buffer);
  } catch (EnvoyException& e) {
    ENVOY_LOG(info, "mongo decoding error: {}", e.what());
    stats_.decoding_error_.inc();
    sniffing_ = false;
  }
}

void ProxyFilter::logMessage(Message& message, bool full) {
  if (access_log_ &&
      runtime_.snapshot().featureEnabled(MongoRuntimeConfig::get().LoggingEnabled, 100)) {
    access_log_->logMessage(message, full, read_callbacks_->upstreamHost().get());
  }
}

void ProxyFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (delay_timer_) {
      delay_timer_->disableTimer();
      delay_timer_.reset();
    }

    if (drain_close_timer_) {
      drain_close_timer_->disableTimer();
      drain_close_timer_.reset();
    }
  }

  if (event == Network::ConnectionEvent::RemoteClose && !active_query_list_.empty()) {
    stats_.cx_destroy_local_with_active_rq_.inc();
  }

  if (event == Network::ConnectionEvent::LocalClose && !active_query_list_.empty()) {
    stats_.cx_destroy_remote_with_active_rq_.inc();
  }
}

Network::FilterStatus ProxyFilter::onData(Buffer::Instance& data, bool) {
  read_buffer_.add(data);
  doDecode(read_buffer_);

  return delay_timer_ ? Network::FilterStatus::StopIteration : Network::FilterStatus::Continue;
}

Network::FilterStatus ProxyFilter::onWrite(Buffer::Instance& data, bool) {
  write_buffer_.add(data);
  doDecode(write_buffer_);
  return Network::FilterStatus::Continue;
}

DecoderPtr ProdProxyFilter::createDecoder(DecoderCallbacks& callbacks) {
  return DecoderPtr{new DecoderImpl(callbacks)};
}

absl::optional<uint64_t> ProxyFilter::delayDuration() {
  absl::optional<uint64_t> result;

  if (!fault_config_) {
    return result;
  }

  if (!runtime_.snapshot().featureEnabled(MongoRuntimeConfig::get().FixedDelayPercent,
                                          fault_config_->delayPercent())) {
    return result;
  }

  const uint64_t duration = runtime_.snapshot().getInteger(
      MongoRuntimeConfig::get().FixedDelayDurationMs, fault_config_->delayDuration());

  // Delay only if the duration is > 0ms.
  if (duration > 0) {
    result = duration;
  }

  return result;
}

void ProxyFilter::delayInjectionTimerCallback() {
  delay_timer_.reset();

  // Continue request processing.
  read_callbacks_->continueReading();
}

void ProxyFilter::tryInjectDelay() {
  // Do not try to inject delays if there is an active delay.
  // Make sure to capture stats for the request otherwise.
  if (delay_timer_) {
    return;
  }

  const absl::optional<uint64_t> delay_ms = delayDuration();

  if (delay_ms) {
    delay_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { delayInjectionTimerCallback(); });
    delay_timer_->enableTimer(std::chrono::milliseconds(delay_ms.value()));
    stats_.delays_injected_.inc();
  }
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
