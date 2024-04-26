#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/thrift_to_metadata/v3/thrift_to_metadata.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/matchers.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

/**
 * All stats for the Thrift to Metadata filter. @see stats_macros.h
 */
#define ALL_THRIFT_TO_METADATA_FILTER_STATS(COUNTER)                                               \
  COUNTER(success)                                                                                 \
  COUNTER(mismatched_content_type)                                                                 \
  COUNTER(no_body)                                                                                 \
  COUNTER(invalid_thrift_body)

/**
 * Wrapper struct for Thrift to Metadata filter stats. @see stats_macros.h
 */
struct ThriftToMetadataStats {
  ALL_THRIFT_TO_METADATA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using ProtoRule = envoy::extensions::filters::http::thrift_to_metadata::v3::Rule;
using KeyValuePair = envoy::extensions::filters::http::thrift_to_metadata::v3::KeyValuePair;

/**
 * Configuration for the Thrift to Metadata filter.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata&
                   proto_config,
               Stats::Scope& scope);
};

/**
 * HTTP Thrift to Metadata Filter.
 */
class Filter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(std::shared_ptr<FilterConfig> config) : config_(config){};

private:
  std::shared_ptr<FilterConfig> config_;
};

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
