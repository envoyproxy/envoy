#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

class Config : public ::Envoy::Router::RouteSpecificFilterConfig,
               public Logger::Loggable<Logger::Id::config> {
public:
  Config(const envoy::extensions::filters::http::set_metadata::v3::Config& config);

  absl::string_view metadataNamespace() const { return namespace_; }
  const ProtobufWkt::Struct& value() { return value_; }

private:
  std::string namespace_;
  ProtobufWkt::Struct value_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

class SetMetadataFilter : public Http::PassThroughDecoderFilter,
                          public Logger::Loggable<Logger::Id::filter> {
public:
  SetMetadataFilter(const ConfigSharedPtr config);
  ~SetMetadataFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override {}

  // StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override;

private:
  const ConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
};

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
