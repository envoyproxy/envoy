#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"
#include "common/grpc/async_client_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * All load reporter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_HDS_STATS(COUNTER)                                                           \
  COUNTER(requests)                                                                                \
  COUNTER(responses)                                                                               \
  COUNTER(errors)
// clang-format on

/**
 * Struct definition for all load reporter stats. @see stats_macros.h
 */
struct HdsDelegateStats {
  ALL_HDS_STATS(GENERATE_COUNTER_STRUCT)
};

class HdsDelegate
    : Grpc::TypedAsyncStreamCallbacks<envoy::service::discovery::v2::HealthCheckSpecifier>,
      Logger::Loggable<Logger::Id::upstream> {
public:
  HdsDelegate(const envoy::api::v2::core::Node& node, Stats::Scope& scope,
              Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher);

  // Grpc::TypedAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override;
  void onReceiveMessage(
      std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_DELAY_MS = 5000;

private:
  void setRetryTimer();
  void establishNewStream();
  void sendHealthCheckRequest();
  void handleFailure();

  HdsDelegateStats stats_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncStream* stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr retry_timer_;
  Event::TimerPtr response_timer_;
  envoy::service::discovery::v2::HealthCheckRequest health_check_request_;
  std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier> health_check_message_;
  std::vector<std::string> clusters_;
};

typedef std::unique_ptr<HdsDelegate> HdsDelegatePtr;

} // namespace Upstream
} // namespace Envoy
