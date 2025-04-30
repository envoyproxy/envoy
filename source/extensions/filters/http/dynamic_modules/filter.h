#pragma once

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

using namespace Envoy::Http;

/**
 * A filter that uses a dynamic module and corresponds to a single HTTP stream.
 */
class DynamicModuleHttpFilter : public Http::StreamFilter,
                                public std::enable_shared_from_this<DynamicModuleHttpFilter> {
public:
  DynamicModuleHttpFilter(DynamicModuleHttpFilterConfigSharedPtr config) : config_(config) {}
  ~DynamicModuleHttpFilter() override;

  /**
   * Initializes the in-module filter.
   */
  void initializeInModuleFilter();

  // ---------- Http::StreamFilterBase ------------
  void onStreamComplete() override;
  void onDestroy() override;

  // ----------  Http::StreamDecoderFilter  ----------
  FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(RequestTrailerMap&) override;
  FilterMetadataStatus decodeMetadata(MetadataMap&) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }
  void decodeComplete() override;

  // ----------  Http::StreamEncoderFilter  ----------
  Filter1xxHeadersStatus encode1xxHeaders(ResponseHeaderMap&) override;
  FilterHeadersStatus encodeHeaders(ResponseHeaderMap& headers, bool end_stream) override;
  FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus encodeTrailers(ResponseTrailerMap&) override;
  FilterMetadataStatus encodeMetadata(MetadataMap&) override;
  void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }
  void encodeComplete() override;

  void sendLocalReply(Code code, absl::string_view body,
                      std::function<void(ResponseHeaderMap& headers)> modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details);

  // The callbacks for the filter. They are only valid until onDestroy() is called.
  StreamDecoderFilterCallbacks* decoder_callbacks_ = nullptr;
  StreamEncoderFilterCallbacks* encoder_callbacks_ = nullptr;

  RequestHeaderMap* request_headers_ = nullptr;
  RequestTrailerMap* request_trailers_ = nullptr;
  ResponseHeaderMap* response_headers_ = nullptr;
  ResponseTrailerMap* response_trailers_ = nullptr;

  // These are used to hold the current chunk of the request/response body during the decodeData and
  // encodeData callbacks. It is only valid during the call and should not be used outside of the
  // call.
  Buffer::Instance* current_request_body_ = nullptr;
  Buffer::Instance* current_response_body_ = nullptr;

  /**
   * Helper to get the downstream information of the stream.
   */
  StreamInfo::StreamInfo* streamInfo() {
    if (decoder_callbacks_) {
      return &decoder_callbacks_->streamInfo();
    } else if (encoder_callbacks_) {
      return &encoder_callbacks_->streamInfo();
    } else {
      return nullptr;
    }
  }

  /**
   * Helper to get the upstream information of the stream.
   */
  StreamInfo::UpstreamInfo* upstreamInfo() {
    auto stream_info = streamInfo();
    if (stream_info) {
      return stream_info->upstreamInfo().get();
    }
    return nullptr;
  }

private:
  /**
   * This is a helper function to get the `this` pointer as a void pointer which is passed to the
   * various event hooks.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Called when filter is destroyed via onDestroy() or destructor. Forwards the call to the
   * module via on_http_filter_destroy_ and resets in_module_filter_ to null. Subsequent calls are a
   * no-op.
   */
  void destroy();

  // This helps to avoid reentering the module when sending a local reply. For example, if
  // sendLocalReply() is called, encodeHeaders and encodeData will be called again inline on top of
  // the stack calling it, which can be problematic. For example, with Rust, that might cause
  // multiple mutable borrows of the same object. In practice, a module shouldn't need encodeHeaders
  // and encodeData to be called for local reply contents, so we just skip them with this flag.
  bool sent_local_reply_ = false;

  const DynamicModuleHttpFilterConfigSharedPtr config_ = nullptr;
  envoy_dynamic_module_type_http_filter_module_ptr in_module_filter_ = nullptr;
};

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
