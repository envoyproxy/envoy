#pragma once

#include <regex>

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/init/manager.h"

#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Server {
namespace Utility {

// HistogramBucketsMode determines how histogram statistics get reported. Not
// all modes are supported for all formats, with the "Unset" variant allowing
// different formats to have different default behavior.
enum class HistogramBucketsMode { Unset, Summary, Cumulative, Disjoint, Detailed };

void populateFallbackResponseHeaders(Http::Code code, Http::ResponseHeaderMap& header_map);

absl::Status histogramBucketsParam(const Http::Utility::QueryParamsMulti& params,
                                   HistogramBucketsMode& histogram_buckets_mode);

absl::optional<std::string> formatParam(const Http::Utility::QueryParamsMulti& params);

absl::optional<std::string> nonEmptyQueryParam(const Http::Utility::QueryParamsMulti& params,
                                               const std::string& key);

} // namespace Utility
} // namespace Server
} // namespace Envoy
