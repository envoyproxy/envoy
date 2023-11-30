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

enum class HistogramBucketsMode { NoBuckets, Cumulative, Disjoint, Detailed };

void populateFallbackResponseHeaders(Http::Code code, Http::ResponseHeaderMap& header_map);

bool filterParam(Http::Utility::QueryParamsMulti params, Buffer::Instance& response,
                 std::shared_ptr<std::regex>& regex);

absl::Status histogramBucketsParam(const Http::Utility::QueryParamsMulti& params,
                                   HistogramBucketsMode& histogram_buckets_mode);

absl::optional<std::string> formatParam(const Http::Utility::QueryParamsMulti& params);

} // namespace Utility
} // namespace Server
} // namespace Envoy
