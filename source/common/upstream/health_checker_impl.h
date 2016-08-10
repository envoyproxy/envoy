#pragma once

#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/health_checker.h"

#include "common/common/logger.h"
#include "common/http/codec_client.h"
#include "common/json/json_loader.h"
#include "common/network/filter_impl.h"

namespace Upstream {

/**
 * All health checker stats. @see stats_macros.h
 */
// clang-format off
#define ALL_HEALTH_CHECKER_STATS(COUNTER, GAUGE)                                                   \
  COUNTER(attempt)                                                                                 \
  COUNTER(success)                                                                                 \
  COUNTER(failure)                                                                                 \
  COUNTER(timeout)                                                                                 \
  COUNTER(protocol_error)                                                                          \
  COUNTER(verify_cluster)                                                                          \
  GAUGE  (healthy)
// clang-format on

/**
 * Definition of all health checker stats. @see stats_macros.h
 */
struct HealthCheckerStats {
  ALL_HEALTH_CHECKER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Base implementation for both the HTTP and TCP health checker.
 */
class HealthCheckerImplBase : public HealthChecker, protected Logger::Loggable<Logger::Id::hc> {
public:
  // Upstream::HealthChecker
  void addHostCheckCompleteCb(HostStatusCb callback) override { callbacks_.push_back(callback); }

protected:
  struct ActiveHealthCheckSession {
    ActiveHealthCheckSession(HealthCheckerImplBase& parent, HostPtr host);
    virtual ~ActiveHealthCheckSession();

    void handleSuccess();
    void handleFailure(bool timeout);
    virtual void onInterval() PURE;
    virtual void onTimeout() PURE;

    HealthCheckerImplBase& parent_;
    HostPtr host_;
    Event::TimerPtr interval_timer_;
    Event::TimerPtr timeout_timer_;
    uint32_t num_unhealthy_{};
    uint32_t num_healthy_{};
    bool first_check_{true};
  };

  HealthCheckerImplBase(const Cluster& cluster, const Json::Object& config,
                        Event::Dispatcher& dispatcher, Stats::Store& store,
                        Runtime::Loader& runtime, Runtime::RandomGenerator& random);

  std::chrono::milliseconds interval();

  virtual void onClusterMemberUpdate(const std::vector<HostPtr>& hosts_added,
                                     const std::vector<HostPtr>& hosts_removed) PURE;

  const Cluster& cluster_;
  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds timeout_;
  const uint32_t unhealthy_threshold_;
  const uint32_t healthy_threshold_;
  HealthCheckerStats stats_;
  Stats::Store& stat_store_;
  uint64_t local_process_healthy_{};
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;

private:
  void decHealthy();
  HealthCheckerStats generateStats(Stats::Store& store);
  void incHealthy();
  void refreshHealthyStat();
  void runCallbacks(HostPtr host, bool changed_state);

  static const std::chrono::milliseconds NO_TRAFFIC_INTERVAL;

  std::list<HostStatusCb> callbacks_;
  const std::chrono::milliseconds interval_;
  const std::chrono::milliseconds interval_jitter_;
};

/**
 * HTTP health checker implementation. Connection keep alive is used where possible.
 */
class HttpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  HttpHealthCheckerImpl(const Cluster& cluster, const Json::Object& config,
                        Event::Dispatcher& dispatcher, Stats::Store& store,
                        Runtime::Loader& runtime, Runtime::RandomGenerator& random);

  // Upstream::HealthChecker
  void start() override;

private:
  struct HttpActiveHealthCheckSession : public ActiveHealthCheckSession,
                                        public Http::StreamDecoder,
                                        public Http::StreamCallbacks,
                                        public Network::ConnectionCallbacks {
    HttpActiveHealthCheckSession(HttpHealthCheckerImpl& parent, HostPtr host);
    ~HttpActiveHealthCheckSession();

    void onResponseComplete();
    bool isHealthCheckSucceeded();

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;

    // Http::StreamDecoder
    void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(const Buffer::Instance&, bool end_stream) override {
      if (end_stream) {
        onResponseComplete();
      }
    }
    void decodeTrailers(Http::HeaderMapPtr&&) override { onResponseComplete(); }

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason) override;

    // Network::ConnectionCallbacks
    void onBufferChange(Network::ConnectionBufferType, uint64_t, int64_t) override {}
    void onEvent(uint32_t events) override;

    HttpHealthCheckerImpl& parent_;
    Http::CodecClientPtr client_;
    Http::StreamEncoder* request_encoder_{};
    Http::HeaderMapPtr response_headers_;
    bool expect_reset_{};
  };

  typedef std::unique_ptr<HttpActiveHealthCheckSession> HttpActiveHealthCheckSessionPtr;

  virtual Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;

  // HealthChecker
  void onClusterMemberUpdate(const std::vector<HostPtr>& hosts_added,
                             const std::vector<HostPtr>& hosts_removed) override;

  const std::string path_;
  std::unordered_map<HostPtr, HttpActiveHealthCheckSessionPtr> active_sessions_;
  Optional<std::string> service_name_;
};

/**
 * Production implementation of the HTTP health checker that allocates a real codec client.
 */
class ProdHttpHealthCheckerImpl : public HttpHealthCheckerImpl {
public:
  using HttpHealthCheckerImpl::HttpHealthCheckerImpl;

  // HttpHealthCheckerImpl
  Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

/**
 * Utility class for loading a binary health checking config and matching it against a buffer.
 * Split out for ease of testing. The type of matching performed is the following (this is the
 * MongoDB health check request and response):
 *
 * "send": [
    {"binary": "39000000"},
    {"binary": "EEEEEEEE"},
    {"binary": "00000000"},
    {"binary": "d4070000"},
    {"binary": "00000000"},
    {"binary": "746573742e"},
    {"binary": "24636d6400"},
    {"binary": "00000000"},
    {"binary": "FFFFFFFF"},

    {"binary": "13000000"},
    {"binary": "01"},
    {"binary": "70696e6700"},
    {"binary": "000000000000f03f"},
    {"binary": "00"}
   ],
   "receive": [
    {"binary": "EEEEEEEE"},
    {"binary": "01000000"},
    {"binary": "00000000"},
    {"binary": "0000000000000000"},
    {"binary": "00000000"},
    {"binary": "11000000"},
    {"binary": "01"},
    {"binary": "6f6b"},
    {"binary": "00000000000000f03f"},
    {"binary": "00"}
   ]
 *
 * During each health check cycle, all of the "send" bytes are sent to the target server. Each
 * binary block can be of arbitrary length and is just concatenated together when sent.
 *
 * On the receive side, "fuzzy" matching is performed such that each binary block must be found,
 * and in the order specified, but not necessarly contiguous. Thus, in the example above,
 * "FFFFFFFF" could be inserted in the response between "EEEEEEEE" and "01000000" and the check
 * would still pass.
 */
class TcpHealthCheckMatcher {
public:
  typedef std::list<std::vector<uint8_t>> MatchSegments;

  static MatchSegments loadJsonBytes(const std::vector<Json::Object>& byte_array);
  static bool match(const MatchSegments& expected, const Buffer::Instance& buffer);
};

/**
 * TCP health checker implementation.
 */
class TcpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  TcpHealthCheckerImpl(const Cluster& cluster, const Json::Object& config,
                       Event::Dispatcher& dispatcher, Stats::Store& store, Runtime::Loader& runtime,
                       Runtime::RandomGenerator& random);

  // Upstream::HealthChecker
  void start() override;

private:
  struct TcpActiveHealthCheckSession;

  struct TcpSessionCallbacks : public Network::ConnectionCallbacks,
                               public Network::ReadFilterBaseImpl {
    TcpSessionCallbacks(TcpActiveHealthCheckSession& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onBufferChange(Network::ConnectionBufferType, uint64_t, int64_t) override {}
    void onEvent(uint32_t events) override { parent_.onEvent(events); }

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
      parent_.onData(data);
      return Network::FilterStatus::StopIteration;
    }

    TcpActiveHealthCheckSession& parent_;
  };

  struct TcpActiveHealthCheckSession : public ActiveHealthCheckSession {
    TcpActiveHealthCheckSession(TcpHealthCheckerImpl& parent, HostPtr host)
        : ActiveHealthCheckSession(parent, host), parent_(parent) {
      onInterval();
    }

    ~TcpActiveHealthCheckSession();

    void onData(Buffer::Instance& data);
    void onEvent(uint32_t events);

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;

    TcpHealthCheckerImpl& parent_;
    Network::ClientConnectionPtr client_;
    std::shared_ptr<TcpSessionCallbacks> session_callbacks_;
    bool expect_close_{};
  };

  typedef std::unique_ptr<TcpActiveHealthCheckSession> TcpActiveHealthCheckSessionPtr;

  // HealthChecker
  void onClusterMemberUpdate(const std::vector<HostPtr>& hosts_added,
                             const std::vector<HostPtr>& hosts_removed) override;

  const TcpHealthCheckMatcher::MatchSegments send_bytes_;
  const TcpHealthCheckMatcher::MatchSegments receive_bytes_;
  std::unordered_map<HostPtr, TcpActiveHealthCheckSessionPtr> active_sessions_;
};

} // Upstream
