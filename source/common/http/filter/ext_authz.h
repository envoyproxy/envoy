#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/http/ext_authz/v2/ext_authz.pb.h"
#include "envoy/ext_authz/ext_authz.h"
#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/ext_authz/ext_authz_impl.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Http {
namespace ExtAuthz {

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { Internal, External, Both };

/**
 * Global configuration for the HTTP authorization (ext_authz) filter.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::config::filter::http::ext_authz::v2::ExtAuthz& config,
               const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
               Runtime::Loader& runtime, Upstream::ClusterManager& cm)
      : local_info_(local_info), scope_(scope), runtime_(runtime), cm_(cm),
        cluster_name_(config.grpc_service().envoy_grpc().cluster_name()),
        failure_mode_allow_(config.failure_mode_allow()) {}

  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  Runtime::Loader& runtime() { return runtime_; }
  Stats::Scope& scope() { return scope_; }
  std::string cluster() { return cluster_name_; }
  Upstream::ClusterManager& cm() { return cm_; }
  bool failureModeAllow() const { return failure_mode_allow_; }

private:
  const LocalInfo::LocalInfo& local_info_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cm_;
  std::string cluster_name_;
  bool failure_mode_allow_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

/**
 * HTTP ext_authz filter. Depending on the route configuration, this filter calls the global
 * ext_authz service before allowing further filter iteration.
 */
class Filter : public StreamDecoderFilter, public Envoy::ExtAuthz::RequestCallbacks {
public:
  Filter(FilterConfigSharedPtr config, Envoy::ExtAuthz::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // ExtAuthz::RequestCallbacks
  void onComplete(Envoy::ExtAuthz::CheckStatus status) override;

private:
  enum class State { NotStarted, Calling, Complete };
  void initiateCall(const HeaderMap& headers);

  FilterConfigSharedPtr config_;
  Envoy::ExtAuthz::ClientPtr client_;
  StreamDecoderFilterCallbacks* callbacks_{};
  State state_{State::NotStarted};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  bool initiating_call_{};
  envoy::service::auth::v2::CheckRequest check_request_{};
};

} // namespace ExtAuthz
} // namespace Http
} // namespace Envoy
