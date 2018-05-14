#pragma once

#include <unordered_set>

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/non_copyable.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicRouter {

/**
 * See docs/configuration/http_filters/grpc_web_filter.rst
 */
class DynamicRouter : public Http::StreamDecoderFilter, NonCopyable {
public:
  DynamicRouter(Upstream::ClusterManager& cm,Event::Dispatcher& dispatcher) : cm_(cm),dispatcher_(dispatcher) {}
  virtual ~DynamicRouter(){};

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
  Event::Dispatcher& dispatcher_;
};

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
