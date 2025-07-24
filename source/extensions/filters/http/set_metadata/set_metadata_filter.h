#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

#define ALL_SET_METADATA_FILTER_STATS(COUNTER) COUNTER(overwrite_denied)

struct FilterStats {
  ALL_SET_METADATA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

struct UntypedMetadataEntry {
  bool allow_overwrite{};
  std::string metadata_namespace;
  ProtobufWkt::Struct value;
};

struct TypedMetadataEntry {
  bool allow_overwrite{};
  std::string metadata_namespace;
  ProtobufWkt::Any value;
};

struct FormattedMetadataEntry {
  bool allow_overwrite{};
  std::string metadata_namespace;
  Formatter::FormatterPtr formatter;
};

class Config : public ::Envoy::Router::RouteSpecificFilterConfig,
               public Logger::Loggable<Logger::Id::config> {
public:
  // Factory method for FactoryContext which is used in createFilterFactoryFromProtoTyped.
  static absl::StatusOr<std::shared_ptr<Config>>
  create(const envoy::extensions::filters::http::set_metadata::v3::Config& config,
         Stats::Scope& scope, const std::string& stats_prefix,
         Server::Configuration::FactoryContext& factory_context);

  // Factory method for GenericFactoryContext which is used in ServerFactoryContext scenarios.
  static absl::StatusOr<std::shared_ptr<Config>>
  create(const envoy::extensions::filters::http::set_metadata::v3::Config& config,
         Stats::Scope& scope, const std::string& stats_prefix,
         Server::Configuration::GenericFactoryContext& factory_context);

  const std::vector<UntypedMetadataEntry>& untyped() { return untyped_; }
  const std::vector<TypedMetadataEntry>& typed() { return typed_; }
  const std::vector<FormattedMetadataEntry>& formatted() { return formatted_; }
  const FilterStats& stats() const { return stats_; }

private:
  // Private constructor for status-based creation
  Config(const envoy::extensions::filters::http::set_metadata::v3::Config& config,
         Stats::Scope& scope, const std::string& stats_prefix,
         Server::Configuration::GenericFactoryContext& factory_context,
         absl::Status& creation_status);

  static FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  void parseConfig(const envoy::extensions::filters::http::set_metadata::v3::Config& config,
                   Server::Configuration::GenericFactoryContext& factory_context,
                   absl::Status& creation_status);

  std::vector<UntypedMetadataEntry> untyped_;
  std::vector<TypedMetadataEntry> typed_;
  std::vector<FormattedMetadataEntry> formatted_;
  FilterStats stats_;
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
