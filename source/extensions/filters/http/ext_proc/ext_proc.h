#pragma once

#include <chrono>
#include <memory>

#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/http/filter.h"

#include "common/common/logger.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "extensions/filters/http/ext_proc/client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor& config,
               const std::chrono::milliseconds grpc_timeout)
      : failure_mode_allow_(config.failure_mode_allow()), grpc_timeout_(grpc_timeout) {}

  bool failureModeAllow() const { return failure_mode_allow_; }

  const std::chrono::milliseconds& grpcTimeout() const { return grpc_timeout_; }

private:
  const bool failure_mode_allow_;
  const std::chrono::milliseconds grpc_timeout_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class Filter : public Logger::Loggable<Logger::Id::filter>,
               public Http::PassThroughFilter,
               public ExternalProcessorCallbacks {
  enum FilterState {
    IDLE,
    HEADERS,
  };

public:
  Filter(const FilterConfigSharedPtr& config, ExternalProcessorClientPtr&& client)
      : config_(config), client_(std::move(client)) {}

  void onDestroy() override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  // ExternalProcessorCallbacks

  void onReceiveMessage(
      std::unique_ptr<envoy::service::ext_proc::v3alpha::ProcessingResponse>&& response) override;

  void onGrpcError(Grpc::Status::GrpcStatus error) override;

  void onGrpcClose() override;

private:
  const FilterConfigSharedPtr config_;
  const ExternalProcessorClientPtr client_;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_ = nullptr;

  FilterState request_state_ = FilterState::IDLE;
  ExternalProcessorStreamPtr stream_;
  bool stream_closed_ = false;

  Http::HeaderMap* request_headers_ = nullptr;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy