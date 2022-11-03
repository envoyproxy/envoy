#pragma once

#include <functional>
#include <list>

#include "envoy/http/filter.h"
#include "envoy/server/admin.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

/**
 * A terminal HTTP filter that implements server admin functionality.
 */
class AdminFilter : public Http::PassThroughFilter,
                    public AdminStream,
                    Logger::Loggable<Logger::Id::admin> {
public:
  using AdminServerCallbackFunction = std::function<Http::Code(
      absl::string_view path_and_query, Http::ResponseHeaderMap& response_headers,
      Buffer::OwnedImpl& response, AdminFilter& filter)>;

  AdminFilter(Admin::GenRequestFn admin_handler_func);

  // Http::StreamFilterBase
  // Handlers relying on the reference should use addOnDestroyCallback()
  // to add a callback that will notify them when the reference is no
  // longer valid.
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  // AdminStream
  void setEndStreamOnComplete(bool end_stream) override { end_stream_on_complete_ = end_stream; }
  void addOnDestroyCallback(std::function<void()> cb) override;
  Http::StreamDecoderFilterCallbacks& getDecoderFilterCallbacks() const override;
  const Buffer::Instance* getRequestBody() const override;
  const Http::RequestHeaderMap& getRequestHeaders() const override;
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
    return encoder_callbacks_->http1StreamEncoderOptions();
  }
  Http::Utility::QueryParams queryParams() const override;

private:
  /**
   * Called when an admin request has been completely received.
   */
  void onComplete();
  Admin::GenRequestFn admin_handler_fn_;
  Http::RequestHeaderMap* request_headers_{};
  std::list<std::function<void()>> on_destroy_callbacks_;
  bool end_stream_on_complete_ = true;
};

} // namespace Server
} // namespace Envoy
