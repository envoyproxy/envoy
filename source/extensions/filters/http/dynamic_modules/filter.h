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
  DynamicModuleHttpFilter(DynamicModuleHttpFilterConfigSharedPtr dynamic_module)
      : dynamic_module_(dynamic_module) {}
  ~DynamicModuleHttpFilter() override = default;

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

  // The callbacks for the filter. They are only valid until onDestroy() is called.
  StreamDecoderFilterCallbacks* decoder_callbacks_ = nullptr;
  StreamEncoderFilterCallbacks* encoder_callbacks_ = nullptr;

private:
  /**
   * This is a helper function to get the `this` pointer as a void pointer which is passed to the
   * various event hooks.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  const DynamicModuleHttpFilterConfigSharedPtr dynamic_module_ = nullptr;
  envoy_dynamic_module_type_http_filter_module_ptr in_module_filter_ = nullptr;
};

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
