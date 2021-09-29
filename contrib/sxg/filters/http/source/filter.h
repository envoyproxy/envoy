#pragma once

#include "envoy/stats/scope.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"
#include "contrib/sxg/filters/http/source/encoder.h"
#include "contrib/sxg/filters/http/source/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

/**
 * Transaction flow:
 * 1. check accept request header for whether client can accept sxg
 * 2. check response headers for flag to indicate whether downstream wants SXG encoding
 * 3. if both true, buffer response body until stream end and then run through the libsxg encoder
 * thingy
 *
 */
class Filter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const FilterConfigSharedPtr& config, const EncoderPtr& encoder)
      : config_(config), encoder_(encoder) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override;

  // Http::StreamEncodeFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

private:
  friend class FilterTest;

  bool client_accept_sxg_{false};
  bool should_encode_sxg_{false};
  std::shared_ptr<FilterConfig> config_;
  Http::ResponseHeaderMap* response_headers_;
  uint64_t data_total_{0};
  bool finished_{false};
  const EncoderPtr& encoder_;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_;

  void doSxg();

  const absl::string_view urlStripQueryFragment(absl::string_view path) const;

  bool clientAcceptSXG(const Http::RequestHeaderMap& headers);
  bool shouldEncodeSXG(const Http::ResponseHeaderMap& headers);
  bool encoderBufferLimitReached(uint64_t buffer_length);
  const Http::LowerCaseString& xCanAcceptSxgKey() const;
  const std::string& xCanAcceptSxgValue() const;
  const Http::LowerCaseString& xShouldEncodeSxgKey() const;
  const std::string& htmlContentType() const;
  const std::string& sxgContentTypeUnversioned() const;
  const std::string& acceptedSxgVersion() const;
  const std::string& sxgContentType() const;
};

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
