#include "source/extensions/filters/http/cache/http_cache.h"

#include <algorithm>
#include <ostream>
#include <vector>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/deterministic_hash.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

size_t stableHashKey(const Key& key) { return DeterministicProtoHash::hash(key); }

LookupRequest::LookupRequest(Key&& key, Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher), key_(key) {}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
