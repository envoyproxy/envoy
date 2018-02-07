#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/api/v2/core/health_check.pb.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/redis/conn_pool.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/health_checker.h"

#include "common/common/logger.h"
#include "common/grpc/codec.h"
#include "common/http/codec_client.h"
#include "common/network/filter_impl.h"
#include "common/protobuf/protobuf.h"

#include "src/proto/grpc/health/v1/health.pb.h"

namespace Envoy {
namespace Upstream {

/**
 * Factory for creating health checker implementations.
 */
class HealthCheckerFactory {
public:
  /**
   * Create a health checker.
   * @param hc_config supplies the health check proto.
   * @param cluster supplies the owning cluster.
   * @param runtime supplies the runtime loader.
   * @param random supplies the random generator.
   * @param dispatcher supplies the dispatcher.
   * @return a health checker.
   */
  static HealthCheckerSharedPtr create(const envoy::api::v2::core::HealthCheck& hc_config,
                                       Upstream::Cluster& cluster, Runtime::Loader& runtime,
                                       Runtime::RandomGenerator& random,
                                       Event::Dispatcher& dispatcher);
};

/**
 * All health checker stats. @see stats_macros.h
 */
// clang-format off
#define ALL_HEALTH_CHECKER_STATS(COUNTER, GAUGE)                                                   \
  COUNTER(attempt)                                                                                 \
  COUNTER(success)                                                                                 \
  COUNTER(failure)                                                                                 \
  COUNTER(passive_failure)                                                                         \
  COUNTER(network_failure)                                                                         \
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
 * Base implementation for all health checkers.
 */
class HealthCheckerImplBase : public HealthChecker,
                              protected Logger::Loggable<Logger::Id::hc>,
                              public std::enable_shared_from_this<HealthCheckerImplBase> {
public:
  // Upstream::HealthChecker
  void addHostCheckCompleteCb(HostStatusCb callback) override { callbacks_.push_back(callback); }
  void start() override;

protected:
  class ActiveHealthCheckSession {
  public:
    enum class FailureType { Active, Passive, Network };

    virtual ~ActiveHealthCheckSession();
    void setUnhealthy(FailureType type);
    void start() { onIntervalBase(); }

  protected:
    ActiveHealthCheckSession(HealthCheckerImplBase& parent, HostSharedPtr host);

    void handleSuccess();
    void handleFailure(FailureType type);

    HostSharedPtr host_;

  private:
    virtual void onInterval() PURE;
    void onIntervalBase();
    virtual void onTimeout() PURE;
    void onTimeoutBase();

    HealthCheckerImplBase& parent_;
    Event::TimerPtr interval_timer_;
    Event::TimerPtr timeout_timer_;
    uint32_t num_unhealthy_{};
    uint32_t num_healthy_{};
    bool first_check_{true};
  };

  typedef std::unique_ptr<ActiveHealthCheckSession> ActiveHealthCheckSessionPtr;

  HealthCheckerImplBase(const Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Runtime::RandomGenerator& random);

  virtual ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) PURE;

  const Cluster& cluster_;
  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds timeout_;
  const uint32_t unhealthy_threshold_;
  const uint32_t healthy_threshold_;
  HealthCheckerStats stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const bool reuse_connection_;

private:
  struct HealthCheckHostMonitorImpl : public HealthCheckHostMonitor {
    HealthCheckHostMonitorImpl(const std::shared_ptr<HealthCheckerImplBase>& health_checker,
                               const HostSharedPtr& host)
        : health_checker_(health_checker), host_(host) {}

    // Upstream::HealthCheckHostMonitor
    void setUnhealthy() override;

    std::weak_ptr<HealthCheckerImplBase> health_checker_;
    std::weak_ptr<Host> host_;
  };

  void addHosts(const HostVector& hosts);
  void decHealthy();
  HealthCheckerStats generateStats(Stats::Scope& scope);
  void incHealthy();
  std::chrono::milliseconds interval() const;
  void onClusterMemberUpdate(const HostVector& hosts_added, const HostVector& hosts_removed);
  void refreshHealthyStat();
  void runCallbacks(HostSharedPtr host, bool changed_state);
  void setUnhealthyCrossThread(const HostSharedPtr& host);

  static const std::chrono::milliseconds NO_TRAFFIC_INTERVAL;

  std::list<HostStatusCb> callbacks_;
  const std::chrono::milliseconds interval_;
  const std::chrono::milliseconds interval_jitter_;
  std::unordered_map<HostSharedPtr, ActiveHealthCheckSessionPtr> active_sessions_;
  uint64_t local_process_healthy_{};
};

/**
 * HTTP health checker implementation. Connection keep alive is used where possible.
 */
class HttpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  HttpHealthCheckerImpl(const Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Runtime::RandomGenerator& random);

private:
  struct HttpActiveHealthCheckSession : public ActiveHealthCheckSession,
                                        public Http::StreamDecoder,
                                        public Http::StreamCallbacks {
    HttpActiveHealthCheckSession(HttpHealthCheckerImpl& parent, const HostSharedPtr& host);
    ~HttpActiveHealthCheckSession();

    void onResponseComplete();
    bool isHealthCheckSucceeded();

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;

    // Http::StreamDecoder
    void decode100ContinueHeaders(Http::HeaderMapPtr&&) override {}
    void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance&, bool end_stream) override {
      if (end_stream) {
        onResponseComplete();
      }
    }
    void decodeTrailers(Http::HeaderMapPtr&&) override { onResponseComplete(); }

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    void onEvent(Network::ConnectionEvent event);

    class ConnectionCallbackImpl : public Network::ConnectionCallbacks {
    public:
      ConnectionCallbackImpl(HttpActiveHealthCheckSession& parent) : parent_(parent) {}
      // Network::ConnectionCallbacks
      void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(event); }
      void onAboveWriteBufferHighWatermark() override {}
      void onBelowWriteBufferLowWatermark() override {}

    private:
      HttpActiveHealthCheckSession& parent_;
    };

    ConnectionCallbackImpl connection_callback_impl_{*this};
    HttpHealthCheckerImpl& parent_;
    Http::CodecClientPtr client_;
    Http::StreamEncoder* request_encoder_{};
    Http::HeaderMapPtr response_headers_;
    bool expect_reset_{};
  };

  typedef std::unique_ptr<HttpActiveHealthCheckSession> HttpActiveHealthCheckSessionPtr;

  virtual Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<HttpActiveHealthCheckSession>(*this, host);
  }

  const std::string path_;
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

  static MatchSegments loadProtoBytes(
      const Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload>& byte_array);
  static bool match(const MatchSegments& expected, const Buffer::Instance& buffer);
};

/**
 * TCP health checker implementation.
 */
class TcpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  TcpHealthCheckerImpl(const Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
                       Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                       Runtime::RandomGenerator& random);

private:
  struct TcpActiveHealthCheckSession;

  struct TcpSessionCallbacks : public Network::ConnectionCallbacks,
                               public Network::ReadFilterBaseImpl {
    TcpSessionCallbacks(TcpActiveHealthCheckSession& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(event); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
      parent_.onData(data);
      return Network::FilterStatus::StopIteration;
    }

    TcpActiveHealthCheckSession& parent_;
  };

  struct TcpActiveHealthCheckSession : public ActiveHealthCheckSession {
    TcpActiveHealthCheckSession(TcpHealthCheckerImpl& parent, const HostSharedPtr& host)
        : ActiveHealthCheckSession(parent, host), parent_(parent) {}
    ~TcpActiveHealthCheckSession();

    void onData(Buffer::Instance& data);
    void onEvent(Network::ConnectionEvent event);

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;

    TcpHealthCheckerImpl& parent_;
    Network::ClientConnectionPtr client_;
    std::shared_ptr<TcpSessionCallbacks> session_callbacks_;
  };

  typedef std::unique_ptr<TcpActiveHealthCheckSession> TcpActiveHealthCheckSessionPtr;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<TcpActiveHealthCheckSession>(*this, host);
  }

  const TcpHealthCheckMatcher::MatchSegments send_bytes_;
  const TcpHealthCheckMatcher::MatchSegments receive_bytes_;
};

/**
 * Redis health checker implementation. Sends PING and expects PONG.
 */
class RedisHealthCheckerImpl : public HealthCheckerImplBase {
public:
  RedisHealthCheckerImpl(const Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
                         Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                         Runtime::RandomGenerator& random,
                         Redis::ConnPool::ClientFactory& client_factory);

  static const Redis::RespValue& healthCheckRequest() {
    static HealthCheckRequest* request = new HealthCheckRequest();
    return request->request_;
  }

private:
  struct RedisActiveHealthCheckSession : public ActiveHealthCheckSession,
                                         public Redis::ConnPool::Config,
                                         public Redis::ConnPool::PoolCallbacks,
                                         public Network::ConnectionCallbacks {
    RedisActiveHealthCheckSession(RedisHealthCheckerImpl& parent, const HostSharedPtr& host);
    ~RedisActiveHealthCheckSession();

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;

    // Redis::ConnPool::Config
    bool disableOutlierEvents() const override { return true; }
    std::chrono::milliseconds opTimeout() const override {
      // Allow the main HC infra to control timeout.
      return parent_.timeout_ * 2;
    }

    // Redis::ConnPool::PoolCallbacks
    void onResponse(Redis::RespValuePtr&& value) override;
    void onFailure() override;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    RedisHealthCheckerImpl& parent_;
    Redis::ConnPool::ClientPtr client_;
    Redis::ConnPool::PoolRequest* current_request_{};
  };

  struct HealthCheckRequest {
    HealthCheckRequest();

    Redis::RespValue request_;
  };

  typedef std::unique_ptr<RedisActiveHealthCheckSession> RedisActiveHealthCheckSessionPtr;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<RedisActiveHealthCheckSession>(*this, host);
  }

  Redis::ConnPool::ClientFactory& client_factory_;
};

/**
 * gRPC health checker implementation.
 */
class GrpcHealthCheckerImpl : public HealthCheckerImplBase {
public:
  GrpcHealthCheckerImpl(const Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Runtime::RandomGenerator& random);

private:
  struct GrpcActiveHealthCheckSession : public ActiveHealthCheckSession,
                                        public Http::StreamDecoder,
                                        public Http::StreamCallbacks {
    GrpcActiveHealthCheckSession(GrpcHealthCheckerImpl& parent, const HostSharedPtr& host);
    ~GrpcActiveHealthCheckSession();

    void onRpcComplete(Grpc::Status::GrpcStatus grpc_status, const std::string& grpc_message,
                       bool end_stream);
    bool isHealthCheckSucceeded(Grpc::Status::GrpcStatus grpc_status) const;
    void resetState();
    void logHealthCheckStatus(Grpc::Status::GrpcStatus grpc_status,
                              const std::string& grpc_message);

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;

    // Http::StreamDecoder
    void decode100ContinueHeaders(Http::HeaderMapPtr&&) override {}
    void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance&, bool end_stream) override;
    void decodeTrailers(Http::HeaderMapPtr&&) override;

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    void onEvent(Network::ConnectionEvent event);
    void onGoAway();

    class ConnectionCallbackImpl : public Network::ConnectionCallbacks {
    public:
      ConnectionCallbackImpl(GrpcActiveHealthCheckSession& parent) : parent_(parent) {}
      // Network::ConnectionCallbacks
      void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(event); }
      void onAboveWriteBufferHighWatermark() override {}
      void onBelowWriteBufferLowWatermark() override {}

    private:
      GrpcActiveHealthCheckSession& parent_;
    };

    class HttpConnectionCallbackImpl : public Http::ConnectionCallbacks {
    public:
      HttpConnectionCallbackImpl(GrpcActiveHealthCheckSession& parent) : parent_(parent) {}
      // Http::ConnectionCallbacks
      void onGoAway() override { parent_.onGoAway(); }

    private:
      GrpcActiveHealthCheckSession& parent_;
    };

    ConnectionCallbackImpl connection_callback_impl_{*this};
    HttpConnectionCallbackImpl http_connection_callback_impl_{*this};
    GrpcHealthCheckerImpl& parent_;
    Http::CodecClientPtr client_;
    Http::StreamEncoder* request_encoder_;
    Grpc::Decoder decoder_;
    std::unique_ptr<grpc::health::v1::HealthCheckResponse> health_check_response_;
    // If true, stream reset was initiated by us (GrpcActiveHealthCheckSession), not by HTTP stack,
    // e.g. remote reset. In this case healthcheck status has already been reported, only state
    // cleanup is required.
    bool expect_reset_ = false;
  };

  virtual Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<GrpcActiveHealthCheckSession>(*this, host);
  }

  const Protobuf::MethodDescriptor& service_method_;
  Optional<std::string> service_name_;
};

/**
 * Production implementation of the gRPC health checker that allocates a real codec client.
 */
class ProdGrpcHealthCheckerImpl : public GrpcHealthCheckerImpl {
public:
  using GrpcHealthCheckerImpl::GrpcHealthCheckerImpl;

  // GrpcHealthCheckerImpl
  Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

} // namespace Upstream
} // namespace Envoy
