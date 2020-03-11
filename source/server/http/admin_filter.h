#pragma once

#include <list>

#include "envoy/http/filter.h"

#include "common/common/logger.h"

#include "server/http/admin.h"

namespace Envoy {
namespace Server {

/**
 * A terminal HTTP filter that implements server admin functionality.
 */
class AdminFilter : public Http::StreamDecoderFilter,
                    public AdminStream,
                    Logger::Loggable<Logger::Id::admin> {
public:
  AdminFilter(AdminImpl& parent);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // AdminStream
  void setEndStreamOnComplete(bool end_stream) override { end_stream_on_complete_ = end_stream; }
  void addOnDestroyCallback(std::function<void()> cb) override;
  Http::StreamDecoderFilterCallbacks& getDecoderFilterCallbacks() const override;
  const Buffer::Instance* getRequestBody() const override;
  const Http::RequestHeaderMap& getRequestHeaders() const override;

private:
  /**
   * Called when an admin request has been completely received.
   */
  void onComplete();

  AdminImpl& parent_;
  // Handlers relying on the reference should use addOnDestroyCallback()
  // to add a callback that will notify them when the reference is no
  // longer valid.
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::RequestHeaderMap* request_headers_{};
  std::list<std::function<void()>> on_destroy_callbacks_;
  bool end_stream_on_complete_ = true;
};

} // namespace Server
} // namespace Envoy
