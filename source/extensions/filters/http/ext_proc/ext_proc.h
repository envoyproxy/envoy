#pragma once

#include <memory>

#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/http/filter.h"

#include "common/common/logger.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor& config)
      : failure_mode_allow_(config.failure_mode_allow()) {}

  bool failureModeAllow() const { return failure_mode_allow_; }

private:
  const bool failure_mode_allow_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class Filter : public Logger::Loggable<Logger::Id::filter>, public Http::PassThroughFilter {
public:
  Filter(const FilterConfigSharedPtr& config) : config_(config) {}

  void onDestroy() override;

private:
  FilterConfigSharedPtr config_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy