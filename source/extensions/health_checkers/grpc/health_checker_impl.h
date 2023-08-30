#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/network/socket.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/type/v3/range.pb.h"

#include "source/common/common/dump_state_utils.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/codec_client.h"
#include "source/common/router/header_parser.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/health_checkers/common/health_checker_base_impl.h"

#include "src/proto/grpc/health/v1/health.pb.h"

namespace Envoy {
namespace Upstream {

class GrpcHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.grpc"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::core::v3::HealthCheck::GrpcHealthCheck()};
  }
};

DECLARE_FACTORY(GrpcHealthCheckerFactory);

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
