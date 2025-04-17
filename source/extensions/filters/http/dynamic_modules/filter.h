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

  bool sendHttpCallout(uint32_t callout_id, absl::string_view cluster_name,
                       Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);

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

  const DynamicModuleHttpFilterConfigSharedPtr config_ = nullptr;
  envoy_dynamic_module_type_http_filter_module_ptr in_module_filter_ = nullptr;

  /**
   * This implementation of the AsyncClient::Callbacks is used to handle the response from the HTTP
   * callout from the parent HTTP filter.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(DynamicModuleHttpFilter& filter, uint32_t id)
        : filter_(filter), callout_id_(id) {}
    ~HttpCalloutCallback() override = default;

    void onSuccess(const AsyncClient::Request& request, ResponseMessagePtr&& response) override;
    void onFailure(const AsyncClient::Request& request,
                   Http::AsyncClient::FailureReason reason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                      const Http::ResponseHeaderMap*) override {};

    // This remains false until the callout is actually sent. This allows us to avoid inline
    // calls to onFailure() method when the async client immediately fails the callout.
    bool sent_ = false;
    // This is the request object that is used to send the HTTP callout. It is used to cancel the
    // callout if the filter is destroyed before the callout is completed.
    Http::AsyncClient::Request* request_ = nullptr;

  private:
    DynamicModuleHttpFilter& filter_;
    uint32_t callout_id_;
  };

  absl::flat_hash_map<uint32_t, std::unique_ptr<DynamicModuleHttpFilter::HttpCalloutCallback>>
      http_callouts_;
};

using DynamicModuleHttpFilterSharedPtr = std::shared_ptr<DynamicModuleHttpFilter>;

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
