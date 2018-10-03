#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/core/health_check.pb.h"
#include "envoy/grpc/status.h"

#include "common/common/logger.h"
#include "common/grpc/codec.h"
#include "common/http/codec_client.h"
#include "common/request_info/request_info_impl.h"
#include "common/router/header_parser.h"
#include "common/upstream/health_checker_base_impl.h"

#include "src/proto/grpc/health/v1/health.pb.h"

namespace Envoy {
namespace Upstream {

/**
 * Factory for creating health checker implementations.
 */
class HealthCheckerFactory : public Logger::Loggable<Logger::Id::health_checker> {
public:
  /**
   * Create a health checker.
   * @param hc_config supplies the health check proto.
   * @param cluster supplies the owning cluster.
   * @param runtime supplies the runtime loader.
   * @param random supplies the random generator.
   * @param dispatcher supplies the dispatcher.
   * @param event_logger supplies the event_logger.
   * @return a health checker.
   */
  static HealthCheckerSharedPtr create(const envoy::api::v2::core::HealthCheck& hc_config,
                                       Upstream::Cluster& cluster, Runtime::Loader& runtime,
                                       Runtime::RandomGenerator& random,
                                       Event::Dispatcher& dispatcher,
                                       AccessLog::AccessLogManager& log_manager);
};

/**
 * HTTP health checker implementation. Connection keep alive is used where possible.
 */
class HttpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  HttpHealthCheckerImpl(const Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Runtime::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);

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
    const std::string& hostname_;
    const Http::Protocol protocol_;
    Network::Address::InstanceConstSharedPtr local_address_;
    bool expect_reset_{};
  };

  typedef std::unique_ptr<HttpActiveHealthCheckSession> HttpActiveHealthCheckSessionPtr;

  virtual Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<HttpActiveHealthCheckSession>(*this, host);
  }
  envoy::data::core::v2alpha::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v2alpha::HealthCheckerType::HTTP;
  }

  Http::CodecClient::Type codecClientType(bool use_http2);

  const std::string path_;
  const std::string host_value_;
  absl::optional<std::string> service_name_;
  Router::HeaderParserPtr request_headers_parser_;

protected:
  const Http::CodecClient::Type codec_client_type_;
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
 * and in the order specified, but not necessary contiguous. Thus, in the example above,
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
                       Runtime::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);

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
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
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
  envoy::data::core::v2alpha::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v2alpha::HealthCheckerType::TCP;
  }

  const TcpHealthCheckMatcher::MatchSegments send_bytes_;
  const TcpHealthCheckMatcher::MatchSegments receive_bytes_;
};

/**
 * gRPC health checker implementation.
 */
class GrpcHealthCheckerImpl : public HealthCheckerImplBase {
public:
  GrpcHealthCheckerImpl(const Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Runtime::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);

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
  envoy::data::core::v2alpha::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v2alpha::HealthCheckerType::GRPC;
  }

  const Protobuf::MethodDescriptor& service_method_;
  absl::optional<std::string> service_name_;
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
