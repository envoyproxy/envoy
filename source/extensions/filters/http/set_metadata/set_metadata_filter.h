#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

class Config : public ::Envoy::Router::RouteSpecificFilterConfig,
               public Logger::Loggable<Logger::Id::config> {
public:
  Config(const envoy::extensions::filters::http::set_metadata::v3::Config& config);

  absl::string_view metadataNamespace() const { return namespace_; }
  bool hasUntypedValue() const { return has_untyped_value_; }
  const ProtobufWkt::Struct& untypedValue() { return untyped_value_; }
  bool hasTypedValue() const { return has_typed_value_; }
  const ProtobufWkt::Any& typedValue() { return typed_value_; }
  bool allowOverwrite() const { return allow_overwrite_; }

private:
  absl::string_view namespace_;
  bool has_untyped_value_{true};
  ProtobufWkt::Struct untyped_value_;
  bool has_typed_value_{};
  ProtobufWkt::Any typed_value_;
  // default to true until deprecated ``value`` field is removed
  bool allow_overwrite_{true};
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
