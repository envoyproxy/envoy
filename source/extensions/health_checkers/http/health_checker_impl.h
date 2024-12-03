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

class HttpHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.http"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::core::v3::HealthCheck::HttpHealthCheck()};
  }
};

DECLARE_FACTORY(HttpHealthCheckerFactory);

/**
 * HTTP health checker implementation. Connection keep alive is used where possible.
 */
class HttpHealthCheckerImpl : public HealthCheckerImplBase {
public:
  HttpHealthCheckerImpl(const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
                        Server::Configuration::HealthCheckerFactoryContext& context,
                        HealthCheckEventLoggerPtr&& event_logger);

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
    Network::ConnectionInfoProviderSharedPtr local_connection_info_provider_;
    // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
    const Http::Protocol protocol_;
    bool expect_reset_ : 1;
    bool reuse_connection_ : 1;
    bool request_in_flight_ : 1;
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
  PayloadMatcher::MatchSegments receive_bytes_;
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

} // namespace Upstream
} // namespace Envoy
