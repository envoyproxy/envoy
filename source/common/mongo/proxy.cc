#include "codec_impl.h"
#include "proxy.h"

#include "envoy/common/exception.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/runtime/runtime.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

namespace Mongo {

AccessLog::AccessLog(const std::string& file_name, ::AccessLog::AccessLogManager& log_manager) {
  file_ = log_manager.createAccessLog(file_name);
}

void AccessLog::logMessage(const Message& message, const std::string&, bool full,
                           const Upstream::HostDescription* upstream_host) {
  static const std::string log_format =
      "{{\"time\": \"{}\", \"message\": \"{}\", \"upstream_host\": \"{}\"}}\n";

  SystemTime now = std::chrono::system_clock::now();
  std::string log_line =
      fmt::format(log_format, AccessLogDateTimeFormatter::fromTime(now), message.toString(full),
                  upstream_host ? upstream_host->address()->asString() : "-");

  file_->write(log_line);
}

ProxyFilter::ProxyFilter(const std::string& stat_prefix, Stats::Store& store,
                         Runtime::Loader& runtime, AccessLogPtr access_log)
    : stat_prefix_(stat_prefix), stat_store_(store), stats_(generateStats(stat_prefix, store)),
      runtime_(runtime), access_log_(access_log) {

  if (!runtime_.snapshot().featureEnabled("mongo.connection_logging_enabled", 100)) {
    // If we are not logging at the connection level, just release the shared pointer so that we
    // don't ever log.
    access_log_.reset();
  }
}

ProxyFilter::~ProxyFilter() {}

void ProxyFilter::decodeGetMore(GetMoreMessagePtr&& message) {
  stats_.op_get_more_.inc();
  logMessage(*message, true);
  log_debug("decoded GET_MORE: {}", message->toString(true));
}

void ProxyFilter::decodeInsert(InsertMessagePtr&& message) {
  stats_.op_insert_.inc();
  logMessage(*message, true);
  log_debug("decoded INSERT: {}", message->toString(true));
}

void ProxyFilter::decodeKillCursors(KillCursorsMessagePtr&& message) {
  stats_.op_kill_cursors_.inc();
  logMessage(*message, true);
  log_debug("decoded KILL_CURSORS: {}", message->toString(true));
}

void ProxyFilter::decodeQuery(QueryMessagePtr&& message) {
  stats_.op_query_.inc();
  logMessage(*message, true);
  log_debug("decoded QUERY: {}", message->toString(true));

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
    stat_store_.counter(fmt::format("{}cmd.{}.total", stat_prefix_,
                                    active_query->query_info_.command())).inc();
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
  stat_store_.counter(fmt::format("{}.query.total", prefix)).inc();
  if (query_type == QueryMessageInfo::QueryType::ScatterGet) {
    stat_store_.counter(fmt::format("{}.query.scatter_get", prefix)).inc();
  } else if (query_type == QueryMessageInfo::QueryType::MultiGet) {
    stat_store_.counter(fmt::format("{}.query.multi_get", prefix)).inc();
  }
}

void ProxyFilter::decodeReply(ReplyMessagePtr&& message) {
  stats_.op_reply_.inc();
  logMessage(*message, false);
  log_debug("decoded REPLY: {}", message->toString(true));

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
}

void ProxyFilter::chargeReplyStats(ActiveQuery& active_query, const std::string& prefix,
                                   const ReplyMessage& message) {
  uint64_t reply_documents_byte_size = 0;
  for (const Bson::DocumentPtr& document : message.documents()) {
    reply_documents_byte_size += document->byteSize();
  }

  stat_store_.deliverHistogramToSinks(fmt::format("{}.reply_num_docs", prefix),
                                      message.documents().size());
  stat_store_.deliverHistogramToSinks(fmt::format("{}.reply_size", prefix),
                                      reply_documents_byte_size);
  stat_store_.deliverTimingToSinks(
      fmt::format("{}.reply_time_ms", prefix),
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() -
                                                            active_query.start_time_));
}

void ProxyFilter::doDecode(Buffer::Instance& buffer) {
  if (!sniffing_ || !runtime_.snapshot().featureEnabled("mongo.proxy_enabled", 100)) {
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
    log().info("mongo decoding error: {}", e.what());
    stats_.decoding_error_.inc();
    sniffing_ = false;
  }
}

void ProxyFilter::logMessage(Message& message, bool full) {
  if (access_log_ && runtime_.snapshot().featureEnabled("mongo.logging_enabled", 100)) {
    access_log_->logMessage(message, last_base64_op_, full, read_callbacks_->upstreamHost().get());
  }
}

void ProxyFilter::onEvent(uint32_t event) {
  if ((event & Network::ConnectionEvent::RemoteClose) && !active_query_list_.empty()) {
    stats_.cx_destroy_local_with_active_rq_.inc();
  }
  if ((event & Network::ConnectionEvent::LocalClose) && !active_query_list_.empty()) {
    stats_.cx_destroy_remote_with_active_rq_.inc();
  }
}

Network::FilterStatus ProxyFilter::onData(Buffer::Instance& data) {
  read_buffer_.add(data);
  doDecode(read_buffer_);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ProxyFilter::onWrite(Buffer::Instance& data) {
  write_buffer_.add(data);
  doDecode(write_buffer_);
  return Network::FilterStatus::Continue;
}

DecoderPtr ProdProxyFilter::createDecoder(DecoderCallbacks& callbacks) {
  return DecoderPtr{new DecoderImpl(callbacks)};
}

} // Mongo
