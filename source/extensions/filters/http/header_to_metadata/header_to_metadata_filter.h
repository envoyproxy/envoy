#pragma once

#include <string>

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/header_to_metadata/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

/**
 * Header-To-Metadata examines request/response headers and either copies or
 * moves the values into request metadata based on configuration information.
 */
class HeaderToMetadataFilter : public Http::StreamFilter,
                               public Logger::Loggable<Logger::Id::filter> {
public:
  HeaderToMetadataFilter(const ConfigSharedPtr config);
  ~HeaderToMetadataFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override {}

  // StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

private:
  friend class HeaderToMetadataTest;

  using StructMap = std::map<std::string, Protobuf::Struct>;

  const ConfigSharedPtr config_;
  mutable const Config* effective_config_{nullptr};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};

  /**
   *  writeHeaderToMetadata encapsulates (1) searching for the header and (2) writing it to the
   *  request metadata.
   *  @param headers the map of key-value headers to look through. These could be response or
   *                 request headers depending on whether this is called from the encode state or
   *                 decode state.
   *  @param rules the header-to-metadata mapping set in configuration.
   *  @param callbacks the callback used to fetch the StreamInfo (which is then used to get
   *                   metadata). Callable with both encoder_callbacks_ and decoder_callbacks_.
   *  @param direction whether processing request or response headers for stats collection.
   */
  void writeHeaderToMetadata(Http::HeaderMap& headers, const HeaderToMetadataRules& rules,
                             Http::StreamFilterCallbacks& callbacks, HeaderDirection direction);
  bool addMetadata(StructMap&, const std::string&, const std::string&, std::string, ValueType,
                   ValueEncode, HeaderDirection direction) const;
  void applyKeyValue(std::string&&, const Rule&, const KeyValuePair&, StructMap&,
                     HeaderDirection direction);
  const std::string& decideNamespace(const std::string& nspace) const;
  const Config* getConfig() const;
};

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
