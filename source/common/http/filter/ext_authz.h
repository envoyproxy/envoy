#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/ext_authz/ext_authz.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"

#include "api/filter/http/ext_authz.pb.h"

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
  // @saumoh: TBD : Take care of grpc service != envoy_grpc()
  FilterConfig(const envoy::api::v2::filter::http::ExtAuthz& config,
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
  bool failOpen() const { return failure_mode_allow_; }

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

  ~Filter() {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // ExtAuthz::RequestCallbacks
  void complete(Envoy::ExtAuthz::CheckStatus status) override;

  void setCheckReqGenerator(Envoy::ExtAuthz::CheckRequestGenerator* crg);

private:
  enum class State { NotStarted, Calling, Complete, Responded };
  void initiateCall(const HeaderMap& headers);

  FilterConfigSharedPtr config_;
  Envoy::ExtAuthz::ClientPtr client_;
  StreamDecoderFilterCallbacks* callbacks_{};
  State state_{State::NotStarted};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  bool initiating_call_{};
  Envoy::ExtAuthz::CheckRequestGeneratorPtr check_req_generator_{};
};

} // namespace ExtAuthz
} // namespace Http
} // namespace Envoy
