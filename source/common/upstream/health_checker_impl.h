#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/network/socket.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/type/v3/range.pb.h"

#include "source/common/common/dump_state_utils.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/codec_client.h"
#include "source/common/router/header_parser.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/health_checker_base_impl.h"

#include "src/proto/grpc/health/v1/health.pb.h"

namespace Envoy {
namespace Upstream {

constexpr uint64_t kDefaultMaxBytesInBuffer = 1024;

/**
 * Factory for creating health checker implementations.
 */
class HealthCheckerFactory : public Logger::Loggable<Logger::Id::health_checker> {
public:
  /**
   * Create a health checker.
   * @param health_check_config supplies the health check proto.
   * @param cluster supplies the owning cluster.
   * @param runtime supplies the runtime loader.
   * @param dispatcher supplies the dispatcher.
   * @param log_manager supplies the log_manager.
   * @param validation_visitor message validation visitor instance.
   * @param api reference to the Api object
   * @return a health checker.
   */
  static HealthCheckerSharedPtr
  create(const envoy::config::core::v3::HealthCheck& health_check_config,
         Upstream::Cluster& cluster, Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
         AccessLog::AccessLogManager& log_manager,
         ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);
};

/**
 * Utility class for loading a binary health checking config and matching it against a buffer.
 * Split out for ease of testing. The type of matching performed is the following (this is the
 * MongoDB health check request and response):
 *
 * "send": [
    {"text": "39000000"},
    {"text": "EEEEEEEE"},
    {"text": "00000000"},
    {"text": "d4070000"},
    {"text": "00000000"},
    {"text": "746573742e"},
    {"text": "24636d6400"},
    {"text": "00000000"},
    {"text": "FFFFFFFF"},

    {"text": "13000000"},
    {"text": "01"},
    {"text": "70696e6700"},
    {"text": "000000000000f03f"},
    {"text": "00"}
   ],
   "receive": [
    {"text": "EEEEEEEE"},
    {"text": "01000000"},
    {"text": "00000000"},
    {"text": "0000000000000000"},
    {"text": "00000000"},
    {"text": "11000000"},
    {"text": "01"},
    {"text": "6f6b"},
    {"text": "00000000000000f03f"},
    {"text": "00"}
   ]
 * Each text or binary filed in Payload is converted to a binary block.
 * The text is Hex string by default.
 *
 * During each health check cycle, all of the "send" bytes are sent to the target server. Each
 * binary block can be of arbitrary length and is just concatenated together when sent.
 *
 * On the receive side, "fuzzy" matching is performed such that each binary block must be found,
 * and in the order specified, but not necessary contiguous. Thus, in the example above,
 * "FFFFFFFF" could be inserted in the response between "EEEEEEEE" and "01000000" and the check
 * would still pass.
 *
 */
class PayloadMatcher {
public:
  using MatchSegments = std::list<std::vector<uint8_t>>;

  static MatchSegments loadProtoBytes(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck::Payload>& byte_array);
  static bool match(const MatchSegments& expected, const Buffer::Instance& buffer);
};

/**
 * HTTP health checker implementation. Connection keep alive is used where possible.
 */
class HttpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  HttpHealthCheckerImpl(const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Random::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);

  // Returns the HTTP protocol used for the health checker.
  Http::Protocol protocol() const;

  /**
   * Utility class checking if given http status matches configured expectations.
   */
  class HttpStatusChecker {
  public:
    HttpStatusChecker(
        const Protobuf::RepeatedPtrField<envoy::type::v3::Int64Range>& expected_statuses,
        const Protobuf::RepeatedPtrField<envoy::type::v3::Int64Range>& retriable_statuses,
        uint64_t default_expected_status);

    bool inRetriableRanges(uint64_t http_status) const;
    bool inExpectedRanges(uint64_t http_status) const;

  private:
    static bool inRanges(uint64_t http_status,
                         const std::vector<std::pair<uint64_t, uint64_t>>& ranges);
    static void validateRange(uint64_t start, uint64_t end, absl::string_view range_type);

    std::vector<std::pair<uint64_t, uint64_t>> expected_ranges_;
    std::vector<std::pair<uint64_t, uint64_t>> retriable_ranges_;
  };

private:
  struct HttpActiveHealthCheckSession : public ActiveHealthCheckSession,
                                        public Http::ResponseDecoder,
                                        public Http::StreamCallbacks {
    HttpActiveHealthCheckSession(HttpHealthCheckerImpl& parent, const HostSharedPtr& host);
    ~HttpActiveHealthCheckSession() override;

    void onResponseComplete();
    enum class HealthCheckResult { Succeeded, Degraded, Failed, Retriable };
    HealthCheckResult healthCheckResult();
    bool shouldClose() const;

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    // Http::StreamDecoder
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override {}

    // Http::ResponseDecoder
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void decodeTrailers(Http::ResponseTrailerMapPtr&&) override { onResponseComplete(); }
    void dumpState(std::ostream& os, int indent_level) const override {
      DUMP_STATE_UNIMPLEMENTED(HttpActiveHealthCheckSession);
    }

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason,
                       absl::string_view transport_failure_reason) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    void onEvent(Network::ConnectionEvent event);
    void onGoAway(Http::GoAwayErrorCode error_code);

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

    class HttpConnectionCallbackImpl : public Http::ConnectionCallbacks {
    public:
      HttpConnectionCallbackImpl(HttpActiveHealthCheckSession& parent) : parent_(parent) {}
      // Http::ConnectionCallbacks
      void onGoAway(Http::GoAwayErrorCode error_code) override { parent_.onGoAway(error_code); }

    private:
      HttpActiveHealthCheckSession& parent_;
    };

    ConnectionCallbackImpl connection_callback_impl_{*this};
    HttpConnectionCallbackImpl http_connection_callback_impl_{*this};
    HttpHealthCheckerImpl& parent_;
    Http::CodecClientPtr client_;
    Http::ResponseHeaderMapPtr response_headers_;
    Buffer::InstancePtr response_body_;
    const std::string& hostname_;
    const Http::Protocol protocol_;
    Network::ConnectionInfoProviderSharedPtr local_connection_info_provider_;
    bool expect_reset_{};
    bool reuse_connection_ = false;
    bool request_in_flight_ = false;
  };

  using HttpActiveHealthCheckSessionPtr = std::unique_ptr<HttpActiveHealthCheckSession>;

  virtual Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<HttpActiveHealthCheckSession>(*this, host);
  }
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::HTTP;
  }

  Http::CodecType codecClientType(const envoy::type::v3::CodecClientType& type);

  const std::string path_;
  const std::string host_value_;
  const PayloadMatcher::MatchSegments receive_bytes_;
  const envoy::config::core::v3::RequestMethod method_;
  uint64_t response_buffer_size_;
  absl::optional<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>
      service_name_matcher_;
  Router::HeaderParserPtr request_headers_parser_;
  const HttpStatusChecker http_status_checker_;

protected:
  const Http::CodecType codec_client_type_;
  Random::RandomGenerator& random_generator_;
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
 * TCP health checker implementation.
 */
class TcpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  TcpHealthCheckerImpl(const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
                       Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                       Random::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);

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
    ~TcpActiveHealthCheckSession() override;

    void onData(Buffer::Instance& data);
    void onEvent(Network::ConnectionEvent event);

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    TcpHealthCheckerImpl& parent_;
    Network::ClientConnectionPtr client_;
    std::shared_ptr<TcpSessionCallbacks> session_callbacks_;
    // If true, stream close was initiated by us, not e.g. remote close or TCP reset.
    // In this case healthcheck status already reported, only state cleanup required.
    bool expect_close_{};
  };

  using TcpActiveHealthCheckSessionPtr = std::unique_ptr<TcpActiveHealthCheckSession>;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<TcpActiveHealthCheckSession>(*this, host);
  }
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::TCP;
  }

  const PayloadMatcher::MatchSegments send_bytes_;
  const PayloadMatcher::MatchSegments receive_bytes_;
};

/**
 * gRPC health checker implementation.
 */
class GrpcHealthCheckerImpl : public HealthCheckerImplBase {
public:
  GrpcHealthCheckerImpl(const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Random::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);

private:
  struct GrpcActiveHealthCheckSession : public ActiveHealthCheckSession,
                                        public Http::ResponseDecoder,
                                        public Http::StreamCallbacks {
    GrpcActiveHealthCheckSession(GrpcHealthCheckerImpl& parent, const HostSharedPtr& host);
    ~GrpcActiveHealthCheckSession() override;

    void onRpcComplete(Grpc::Status::GrpcStatus grpc_status, const std::string& grpc_message,
                       bool end_stream);
    bool isHealthCheckSucceeded(Grpc::Status::GrpcStatus grpc_status) const;
    void resetState();
    void logHealthCheckStatus(Grpc::Status::GrpcStatus grpc_status,
                              const std::string& grpc_message);

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    // Http::StreamDecoder
    void decodeData(Buffer::Instance&, bool end_stream) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override {}

    // Http::ResponseDecoder
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void decodeTrailers(Http::ResponseTrailerMapPtr&&) override;
    void dumpState(std::ostream& os, int indent_level) const override {
      DUMP_STATE_UNIMPLEMENTED(GrpcActiveHealthCheckSession);
    }

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason,
                       absl::string_view transport_failure_reason) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    void onEvent(Network::ConnectionEvent event);
    void onGoAway(Http::GoAwayErrorCode error_code);

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
      void onGoAway(Http::GoAwayErrorCode error_code) override { parent_.onGoAway(error_code); }

    private:
      GrpcActiveHealthCheckSession& parent_;
    };

    ConnectionCallbackImpl connection_callback_impl_{*this};
    HttpConnectionCallbackImpl http_connection_callback_impl_{*this};
    GrpcHealthCheckerImpl& parent_;
    Http::CodecClientPtr client_;
    Http::RequestEncoder* request_encoder_;
    Grpc::Decoder decoder_;
    std::unique_ptr<grpc::health::v1::HealthCheckResponse> health_check_response_;
    Network::ConnectionInfoProviderSharedPtr local_connection_info_provider_;
    // If true, stream reset was initiated by us (GrpcActiveHealthCheckSession), not by HTTP stack,
    // e.g. remote reset. In this case healthcheck status has already been reported, only state
    // cleanup is required.
    bool expect_reset_ = false;
    // If true, we received a GOAWAY (NO_ERROR code) and are deferring closing the connection
    // until the active probe completes.
    bool received_no_error_goaway_ = false;
  };

  virtual Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) override {
    return std::make_unique<GrpcActiveHealthCheckSession>(*this, host);
  }
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::GRPC;
  }

protected:
  Random::RandomGenerator& random_generator_;

private:
  const Protobuf::MethodDescriptor& service_method_;
  absl::optional<std::string> service_name_;
  absl::optional<std::string> authority_value_;
  Router::HeaderParserPtr request_headers_parser_;
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
