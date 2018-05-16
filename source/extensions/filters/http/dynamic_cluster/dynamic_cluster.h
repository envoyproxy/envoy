#pragma once

#include <unordered_set>

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/non_copyable.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicCluster {

class DynamicCluster : public Http::StreamDecoderFilter, NonCopyable {
public:
  DynamicCluster(Upstream::ClusterManager& cm) : cm_(cm) {}
  virtual ~DynamicCluster(){};

  // Http::StreamFilterBase
  void onDestroy() override{};

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  };
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  };
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Upstream::ClusterManager& cm_;
};

} // namespace DynamicCluster
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
