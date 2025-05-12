#include "source/extensions/filters/http/cache/cache_entry_utils.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

absl::string_view cacheEntryStatusString(CacheEntryStatus s) {
  switch (s) {
  case CacheEntryStatus::Ok:
    return "Ok";
  case CacheEntryStatus::Unusable:
    return "Unusable";
  case CacheEntryStatus::RequiresValidation:
    return "RequiresValidation";
  case CacheEntryStatus::FoundNotModified:
    return "FoundNotModified";
  case CacheEntryStatus::LookupError:
    return "LookupError";
  }
  IS_ENVOY_BUG(absl::StrCat("Unexpected CacheEntryStatus: ", s));
  return "UnexpectedCacheEntryStatus";
}

std::ostream& operator<<(std::ostream& os, CacheEntryStatus status) {
  return os << cacheEntryStatusString(status);
}

namespace {
const absl::flat_hash_set<Http::LowerCaseString> headersNotToUpdate() {
  CONSTRUCT_ON_FIRST_USE(
      absl::flat_hash_set<Http::LowerCaseString>,
      // Content range should not be changed upon validation
      Http::Headers::get().ContentRange,

      // Headers that describe the body content should never be updated.
      Http::Headers::get().ContentLength,

      // It does not make sense for this level of the code to be updating the ETag, when
      // presumably the cached_response_headers reflect this specific ETag.
      Http::CustomHeaders::get().Etag,

      // We don't update the cached response on a Vary; we just delete it
      // entirely. So don't bother copying over the Vary header.
      Http::CustomHeaders::get().Vary);
}
} // namespace

void applyHeaderUpdate(const Http::ResponseHeaderMap& new_headers,
                       Http::ResponseHeaderMap& headers_to_update) {
  // Assumptions:
  // 1. The internet is fast, i.e. we get the result as soon as the server sends it.
  //    Race conditions would not be possible because we are always processing up-to-date data.
  // 2. No key collision for etag. Therefore, if etag matches it's the same resource.
  // 3. Backend is correct. etag is being used as a unique identifier to the resource

  // use other header fields provided in the new response to replace all instances
  // of the corresponding header fields in the stored response

  // `updatedHeaderFields` makes sure each field is only removed when we update the header
  // field for the first time to handle the case where incoming headers have repeated values
  absl::flat_hash_set<Http::LowerCaseString> updatedHeaderFields;
  new_headers.iterate(
      [&headers_to_update, &updatedHeaderFields](
          const Http::HeaderEntry& incoming_response_header) -> Http::HeaderMap::Iterate {
        Http::LowerCaseString lower_case_key{incoming_response_header.key().getStringView()};
        absl::string_view incoming_value{incoming_response_header.value().getStringView()};
        if (headersNotToUpdate().contains(lower_case_key)) {
          return Http::HeaderMap::Iterate::Continue;
        }
        if (!updatedHeaderFields.contains(lower_case_key)) {
          headers_to_update.setCopy(lower_case_key, incoming_value);
          updatedHeaderFields.insert(lower_case_key);
        } else {
          headers_to_update.addCopy(lower_case_key, incoming_value);
        }
        return Http::HeaderMap::Iterate::Continue;
      });
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
