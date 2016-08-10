#pragma once

#include "utility.h"

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/time.h"
#include "envoy/mongo/codec.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/network/filter_impl.h"

namespace Mongo {

/**
 * All mongo proxy stats. @see stats_macros.h
 */
// clang-format off
#define ALL_MONGO_PROXY_STATS(COUNTER, GAUGE, TIMER)                                               \
  COUNTER(decoding_error)                                                                          \
  COUNTER(op_get_more)                                                                             \
  COUNTER(op_insert)                                                                               \
  COUNTER(op_kill_cursors)                                                                         \
  COUNTER(op_query)                                                                                \
  COUNTER(op_query_tailable_cursor)                                                                \
  COUNTER(op_query_no_cursor_timeout)                                                              \
  COUNTER(op_query_await_data)                                                                     \
  COUNTER(op_query_exhaust)                                                                        \
  COUNTER(op_query_scatter_get)                                                                    \
  COUNTER(op_query_multi_get)                                                                      \
  GAUGE  (op_query_active)                                                                         \
  COUNTER(op_reply)                                                                                \
  COUNTER(op_reply_cursor_not_found)                                                               \
  COUNTER(op_reply_query_failure)                                                                  \
  COUNTER(op_reply_valid_cursor)                                                                   \
  COUNTER(cx_destroy_local_with_active_rq)                                                         \
  COUNTER(cx_destroy_remote_with_active_rq)
// clang-format on

/**
 * Struct definition for all mongo proxy stats. @see stats_macros.h
 */
struct MongoProxyStats {
  ALL_MONGO_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_TIMER_STRUCT)
};

/**
 * Access logger for mongo messages.
 */
class AccessLog : public ::AccessLog::AccessLog {
public:
  AccessLog(Api::Api& api, const std::string& file_name, Event::Dispatcher& dispatcher,
            Thread::BasicLockable& lock, Stats::Store& stats_store);
  ~AccessLog();

  void logMessage(const Message& message, const std::string& base64, bool full,
                  const Upstream::HostDescription* upstream_host);
  void reopen() override;

private:
  Filesystem::FilePtr file_;
};

typedef std::shared_ptr<AccessLog> AccessLogPtr;

/**
 * A sniffing filter for mongo traffic. The current implementation makes a copy of read/written
 * data, decodes it, and generates stats.
 */
class ProxyFilter : public Network::Filter,
                    public DecoderCallbacks,
                    public Network::ConnectionCallbacks,
                    Logger::Loggable<Logger::Id::mongo> {
public:
  ProxyFilter(const std::string& stat_prefix, Stats::Store& store, Runtime::Loader& runtime,
              AccessLogPtr access_log);
  ~ProxyFilter();

  virtual DecoderPtr createDecoder(DecoderCallbacks& callbacks) PURE;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data) override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    read_callbacks_->connection().addConnectionCallbacks(*this);
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data) override;

  // Mongo::DecoderCallback
  void decodeBase64(std::string&& message) override { last_base64_op_ = std::move(message); }
  void decodeGetMore(GetMoreMessagePtr&& message) override;
  void decodeInsert(InsertMessagePtr&& message) override;
  void decodeKillCursors(KillCursorsMessagePtr&& message) override;
  void decodeQuery(QueryMessagePtr&& message) override;
  void decodeReply(ReplyMessagePtr&& message) override;

  // Network::ConnectionCallbacks
  void onEvent(uint32_t event) override;
  void onBufferChange(Network::ConnectionBufferType, uint64_t, int64_t) override {}

private:
  struct ActiveQuery {
    ActiveQuery(ProxyFilter& parent, QueryMessagePtr&& query)
        : parent_(parent), query_(std::move(query)), start_time_(std::chrono::system_clock::now()) {
      parent_.stats_.op_query_active_.inc();
    }

    ~ActiveQuery() { parent_.stats_.op_query_active_.dec(); }

    ProxyFilter& parent_;
    QueryMessagePtr query_;
    SystemTime start_time_;
  };

  typedef std::unique_ptr<ActiveQuery> ActiveQueryPtr;

  MongoProxyStats generateStats(const std::string& prefix, Stats::Store& store) {
    return MongoProxyStats{ALL_MONGO_PROXY_STATS(POOL_COUNTER_PREFIX(store, prefix),
                                                 POOL_GAUGE_PREFIX(store, prefix),
                                                 POOL_TIMER_PREFIX(store, prefix))};
  }

  void chargeQueryStats(const std::string& prefix, MessageUtility::QueryType query_type);
  void chargeReplyStats(ActiveQuery& active_query, const std::string& prefix,
                        const ReplyMessage& message);
  void doDecode(Buffer::Instance& buffer);
  void logMessage(Message& message, bool full);

  std::unique_ptr<Decoder> decoder_;
  std::string stat_prefix_;
  Stats::Store& stat_store_;
  MongoProxyStats stats_;
  Runtime::Loader& runtime_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  bool sniffing_{true};
  std::list<ActiveQueryPtr> active_query_list_;
  AccessLogPtr access_log_;
  std::string last_base64_op_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

class ProdProxyFilter : public ProxyFilter {
public:
  using ProxyFilter::ProxyFilter;

  // ProxyFilter
  DecoderPtr createDecoder(DecoderCallbacks& callbacks) override;
};

} // Mongo
