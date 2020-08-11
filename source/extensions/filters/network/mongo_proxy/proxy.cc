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
#include "extensions/filters/network/well_known_names.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

class DynamicMetadataKeys {
public:
  const std::string OperationInsert{"insert"};
  const std::string OperationQuery{"query"};
  // TODO: Parse out the delete/update operation from the commands
  const std::string OperationUpdate{"update"};
  const std::string OperationDelete{"delete"};
};

using DynamicMetadataKeysSingleton = ConstSingleton<DynamicMetadataKeys>;

AccessLog::AccessLog(const std::string& file_name, Envoy::AccessLog::AccessLogManager& log_manager,
                     TimeSource& time_source)
    : time_source_(time_source) {
  file_ = log_manager.createAccessLog(file_name);
}

void AccessLog::logMessage(const Message& message, bool full,
                           const Upstream::HostDescription* upstream_host) {
  static const std::string log_format =
      "{{\"time\": \"{}\", \"message\": {}, \"upstream_host\": \"{}\"}}\n";

  SystemTime now = time_source_.systemTime();
  std::string log_line =
      fmt::format(log_format, AccessLogDateTimeFormatter::fromTime(now), message.toString(full),
                  upstream_host ? upstream_host->address()->asString() : "-");

  file_->write(log_line);
}

ProxyFilter::ProxyFilter(const std::string& stat_prefix, Stats::Scope& scope,
                         Runtime::Loader& runtime, AccessLogSharedPtr access_log,
                         const Filters::Common::Fault::FaultDelayConfigSharedPtr& fault_config,
                         const Network::DrainDecision& drain_decision, TimeSource& time_source,
                         bool emit_dynamic_metadata, const MongoStatsSharedPtr& mongo_stats)
    : stat_prefix_(stat_prefix), stats_(generateStats(stat_prefix, scope)), runtime_(runtime),
      drain_decision_(drain_decision), access_log_(access_log), fault_config_(fault_config),
      time_source_(time_source), emit_dynamic_metadata_(emit_dynamic_metadata),
      mongo_stats_(mongo_stats) {
  if (!runtime_.snapshot().featureEnabled(MongoRuntimeConfig::get().ConnectionLoggingEnabled,
                                          100)) {
    // If we are not logging at the connection level, just release the shared pointer so that we
    // don't ever log.
    access_log_.reset();
  }
}

ProxyFilter::~ProxyFilter() { ASSERT(!delay_timer_); }

void ProxyFilter::setDynamicMetadata(std::string operation, std::string resource) {
  ProtobufWkt::Struct metadata(
      (*read_callbacks_->connection()
            .streamInfo()
            .dynamicMetadata()
            .mutable_filter_metadata())[NetworkFilterNames::get().MongoProxy]);
  auto& fields = *metadata.mutable_fields();
  // TODO(rshriram): reverse the resource string (table.db)
  auto& operations = *fields[resource].mutable_list_value();
  operations.add_values()->set_string_value(operation);

  read_callbacks_->connection().streamInfo().setDynamicMetadata(
      NetworkFilterNames::get().MongoProxy, metadata);
}

void ProxyFilter::decodeGetMore(GetMoreMessagePtr&& message) {
  tryInjectDelay();

  stats_.op_get_more_.inc();
  logMessage(*message, true);
  ENVOY_LOG(debug, "decoded GET_MORE: {}", message->toString(true));
}

void ProxyFilter::decodeInsert(InsertMessagePtr&& message) {
  tryInjectDelay();

  if (emit_dynamic_metadata_) {
    setDynamicMetadata(DynamicMetadataKeysSingleton::get().OperationInsert,
                       message->fullCollectionName());
  }

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

  if (emit_dynamic_metadata_) {
    setDynamicMetadata(DynamicMetadataKeysSingleton::get().OperationQuery,
                       message->fullCollectionName());
  }

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
    mongo_stats_->incCounter({mongo_stats_->cmd_,
                              mongo_stats_->getBuiltin(active_query->query_info_.command(),
                                                       mongo_stats_->unknown_command_),
                              mongo_stats_->total_});
  } else {
    // Normal query, get stats on a per collection basis first.
    QueryMessageInfo::QueryType query_type = active_query->query_info_.type();
    Stats::ElementVec names;
    names.reserve(6); // 2 entries are added by chargeQueryStats().
    names.push_back(mongo_stats_->collection_);
    names.push_back(Stats::DynamicName(active_query->query_info_.collection()));
    chargeQueryStats(names, query_type);

    // Callsite stats if we have it.
    if (!active_query->query_info_.callsite().empty()) {
      names.push_back(mongo_stats_->callsite_);
      names.push_back(Stats::DynamicName(active_query->query_info_.callsite()));
      chargeQueryStats(names, query_type);
    }

    // Global stats.
    if (active_query->query_info_.maxTime() < 1) {
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

void ProxyFilter::chargeQueryStats(Stats::ElementVec& names,
                                   QueryMessageInfo::QueryType query_type) {
  // names come in containing {"collection", collection}. Report stats for 1 or
  // 2 variations on this array, and then return with the array in the same
  // state it had on entry. Both of these variations by appending {"query", "total"}.
  size_t orig_size = names.size();
  ASSERT(names.capacity() - orig_size >= 2); // Ensures the caller has reserved() enough memory.
  names.push_back(mongo_stats_->query_);
  names.push_back(mongo_stats_->total_);
  mongo_stats_->incCounter(names);

  // And now replace "total" with either "scatter_get" or "multi_get" if depending on query_type.
  if (query_type == QueryMessageInfo::QueryType::ScatterGet) {
    names.back() = mongo_stats_->scatter_get_;
    mongo_stats_->incCounter(names);
  } else if (query_type == QueryMessageInfo::QueryType::MultiGet) {
    names.back() = mongo_stats_->multi_get_;
    mongo_stats_->incCounter(names);
  }
  names.resize(orig_size);
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
      Stats::ElementVec names{mongo_stats_->cmd_,
                              mongo_stats_->getBuiltin(active_query.query_info_.command(),
                                                       mongo_stats_->unknown_command_)};
      chargeReplyStats(active_query, names, *message);
    } else {
      // Collection stats first.
      Stats::ElementVec names{mongo_stats_->collection_,
                              Stats::DynamicName(active_query.query_info_.collection()),
                              mongo_stats_->query_};
      chargeReplyStats(active_query, names, *message);

      // Callsite stats if we have it.
      if (!active_query.query_info_.callsite().empty()) {
        // Currently, names == {"collection", collection, "query"} and we are going
        // to mutate the array to {"collection", collection, "callsite", callsite, "query"}.
        ASSERT(names.size() == 3);
        names.back() = mongo_stats_->callsite_; // Replaces "query".
        names.push_back(Stats::DynamicName(active_query.query_info_.callsite()));
        names.push_back(mongo_stats_->query_);
        chargeReplyStats(active_query, names, *message);
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

void ProxyFilter::chargeReplyStats(ActiveQuery& active_query, Stats::ElementVec& names,
                                   const ReplyMessage& message) {
  uint64_t reply_documents_byte_size = 0;
  for (const Bson::DocumentSharedPtr& document : message.documents()) {
    reply_documents_byte_size += document->byteSize();
  }

  // Write 3 different histograms; appending 3 different suffixes to the name
  // that was passed in. Here we overwrite the passed-in names, but we restore
  // names to its original state upon return.
  const size_t orig_size = names.size();
  names.push_back(mongo_stats_->reply_num_docs_);
  mongo_stats_->recordHistogram(names, Stats::Histogram::Unit::Unspecified,
                                message.documents().size());
  names[orig_size] = mongo_stats_->reply_size_;
  mongo_stats_->recordHistogram(names, Stats::Histogram::Unit::Bytes, reply_documents_byte_size);
  names[orig_size] = mongo_stats_->reply_time_ms_;
  mongo_stats_->recordHistogram(names, Stats::Histogram::Unit::Milliseconds,
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    time_source_.monotonicTime() - active_query.start_time_)
                                    .count());
  names.resize(orig_size);
}

void ProxyFilter::doDecode(Buffer::Instance& buffer) {
  if (!sniffing_ ||
      !runtime_.snapshot().featureEnabled(MongoRuntimeConfig::get().ProxyEnabled, 100)) {
    // Safety measure just to make sure that if we have a decoding error we keep going and lose
    // stats. This can be removed once we are more confident of this code.
    buffer.drain(buffer.length());
    return;
  }

  // Clear dynamic metadata
  if (emit_dynamic_metadata_) {
    auto& metadata = (*read_callbacks_->connection()
                           .streamInfo()
                           .dynamicMetadata()
                           .mutable_filter_metadata())[NetworkFilterNames::get().MongoProxy];
    metadata.mutable_fields()->clear();
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

absl::optional<std::chrono::milliseconds> ProxyFilter::delayDuration() {
  absl::optional<std::chrono::milliseconds> result;

  if (!fault_config_) {
    return result;
  }

  // Use a default percentage
  const auto percentage = fault_config_->percentage(nullptr);
  if (!runtime_.snapshot().featureEnabled(MongoRuntimeConfig::get().FixedDelayPercent,
                                          percentage)) {
    return result;
  }

  // See if the delay provider has a default delay, if not there is no delay.
  auto config_duration = fault_config_->duration(nullptr);
  if (!config_duration.has_value()) {
    return result;
  }

  const std::chrono::milliseconds duration =
      std::chrono::milliseconds(runtime_.snapshot().getInteger(
          MongoRuntimeConfig::get().FixedDelayDurationMs, config_duration.value().count()));

  // Delay only if the duration is > 0ms.
  if (duration.count() > 0) {
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

  const absl::optional<std::chrono::milliseconds> delay = delayDuration();

  if (delay) {
    delay_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { delayInjectionTimerCallback(); });
    delay_timer_->enableTimer(delay.value());
    stats_.delays_injected_.inc();
  }
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
