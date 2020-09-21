#include "extensions/filters/http/cdn_loop/filter.h"

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "common/common/statusor.h"

#include "extensions/filters/http/cdn_loop/utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {

namespace {
const Http::LowerCaseString CdnLoopHeaderName("CDN-Loop");
constexpr absl::string_view ParseErrorMessage = "Invalid CDN-Loop header in request.";
constexpr absl::string_view ParseErrorDetails = "invalid_cdn_loop_header";
constexpr absl::string_view LoopDetectedMessage = "The server has detected a loop between CDNs.";
constexpr absl::string_view LoopDetectedDetails = "cdn_loop_detected";
} // namespace

Http::FilterHeadersStatus CdnLoopFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                       bool /*end_stream*/) {
  if (const Http::HeaderEntry* header_entry = headers.get(CdnLoopHeaderName);
      header_entry != nullptr) {
    if (StatusOr<int> count =
            countCdnLoopOccurrences(header_entry->value().getStringView(), cdn_id_);
        !count) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, ParseErrorMessage, nullptr,
                                         absl::nullopt, ParseErrorDetails);
      return Http::FilterHeadersStatus::StopIteration;
    } else if (*count > max_allowed_occurrences_) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadGateway, LoopDetectedMessage, nullptr,
                                         absl::nullopt, LoopDetectedDetails);
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  headers.appendCopy(CdnLoopHeaderName, cdn_id_);
  return Http::FilterHeadersStatus::Continue;
}

} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
