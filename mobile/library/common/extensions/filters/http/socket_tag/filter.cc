#include "library/common/extensions/filters/http/socket_tag/filter.h"

#include "envoy/server/filter_config.h"

#include "library/common/network/socket_tag_socket_option_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SocketTag {

Http::FilterHeadersStatus SocketTagFilter::decodeHeaders(Http::RequestHeaderMap& request_headers,
                                                         bool) {
  static auto socket_tag_header = Http::LowerCaseString("x-envoy-mobile-socket-tag");
  Http::RequestHeaderMap::GetResult header = request_headers.get(socket_tag_header);
  if (header.empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // The x-envoy-mobile-socket-tag header must contain a pair of number separated by a comma, e.g.:
  // x-envoy-mobile-socket-tag: 123,456
  // The first number contains the UID and the second contains the traffic stats tag.
  std::string tag_string(header[0]->value().getStringView());
  std::pair<std::string, std::string> data = absl::StrSplit(tag_string, ',');
  uid_t uid;
  uint32_t traffic_stats_tag;
  if (!absl::SimpleAtoi(data.first, &uid) || !absl::SimpleAtoi(data.second, &traffic_stats_tag)) {
    decoder_callbacks_->sendLocalReply(
        Http::Code::BadRequest,
        absl::StrCat("Invalid x-envoy-mobile-socket-tag header: ", tag_string), nullptr,
        absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }
  auto options = std::make_shared<Network::Socket::Options>();
  options->push_back(std::make_shared<Network::SocketTagSocketOptionImpl>(uid, traffic_stats_tag));
  decoder_callbacks_->addUpstreamSocketOptions(options);
  request_headers.remove(socket_tag_header);
  return Http::FilterHeadersStatus::Continue;
}

} // namespace SocketTag
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
