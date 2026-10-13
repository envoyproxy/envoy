#pragma once

#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/cache_v2/range_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class CacheReader;

class CacheProgressReceiver {
public:
  virtual void onHeadersInserted(std::unique_ptr<CacheReader> cache_entry,
                                 Http::ResponseHeaderMapPtr headers, bool end_stream) PURE;
  virtual void onBodyInserted(AdjustedByteRange range, bool end_stream) PURE;
  virtual void onTrailersInserted(Http::ResponseTrailerMapPtr trailers) PURE;
  virtual void onInsertFailed(absl::Status status) PURE;
  virtual ~CacheProgressReceiver() = default;
};

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
