#pragma once

#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/cache/range_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheReader;

class CacheProgressReceiver {
public:
  virtual void onHeadersInserted(std::unique_ptr<CacheReader> cache_entry,
                                 Http::ResponseHeaderMapPtr headers, bool end_stream) PURE;
  virtual void onBodyInserted(AdjustedByteRange range, bool end_stream) PURE;
  virtual void onTrailersInserted(Http::ResponseTrailerMapPtr trailers) PURE;
  virtual void onInsertFailed() PURE;
  virtual ~CacheProgressReceiver() = default;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
