#include "source/extensions/formatter/req_without_query/req_without_query.h"

#include <string>

#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

namespace {

void truncate(std::string& str, absl::optional<uint32_t> max_length) {
  if (!max_length) {
    return;
  }

  str = str.substr(0, max_length.value());
}

} // namespace

ReqWithoutQuery::ReqWithoutQuery(const std::string& main_header,
                                 const std::string& alternative_header,
                                 absl::optional<size_t> max_length)
    : main_header_(main_header), alternative_header_(alternative_header), max_length_(max_length) {}

absl::optional<std::string> ReqWithoutQuery::format(const Http::RequestHeaderMap& request,
                                                    const Http::ResponseHeaderMap&,
                                                    const Http::ResponseTrailerMap&,
                                                    const StreamInfo::StreamInfo&,
                                                    absl::string_view) const {
  const Http::HeaderEntry* header = findHeader(request);
  if (!header) {
    return absl::nullopt;
  }

  std::string val = Http::Utility::stripQueryString(header->value());
  truncate(val, max_length_);

  return val;
}

ProtobufWkt::Value ReqWithoutQuery::formatValue(const Http::RequestHeaderMap& request,
                                                const Http::ResponseHeaderMap&,
                                                const Http::ResponseTrailerMap&,
                                                const StreamInfo::StreamInfo&,
                                                absl::string_view) const {
  const Http::HeaderEntry* header = findHeader(request);
  if (!header) {
    return ValueUtil::nullValue();
  }

  std::string val = Http::Utility::stripQueryString(header->value());
  truncate(val, max_length_);
  return ValueUtil::stringValue(val);
}

const Http::HeaderEntry* ReqWithoutQuery::findHeader(const Http::HeaderMap& headers) const {
  const auto header = headers.get(main_header_);

  if (header.empty() && !alternative_header_.get().empty()) {
    const auto alternate_header = headers.get(alternative_header_);
    // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially log all header values.
    return alternate_header.empty() ? nullptr : alternate_header[0];
  }

  return header.empty() ? nullptr : header[0];
}

::Envoy::Formatter::FormatterProviderPtr
ReqWithoutQueryCommandParser::parse(const std::string& token, size_t, size_t) const {
  if (absl::StartsWith(token, "REQ_WITHOUT_QUERY(")) {
    std::string main_header, alternative_header;
    absl::optional<size_t> max_length;

    Envoy::Formatter::SubstitutionFormatParser::parseCommandHeader(
        token, ReqWithoutQueryParamStart, main_header, alternative_header, max_length);
    return std::make_unique<ReqWithoutQuery>(main_header, alternative_header, max_length);
  }

  return nullptr;
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
