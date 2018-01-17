#include "common/router/header_parser.h"

#include <string>

#include "common/common/assert.h"
#include "common/protobuf/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

namespace {

HeaderFormatterPtr parseInternal(const envoy::api::v2::HeaderValueOption& header_value_option) {
  const std::string& format = header_value_option.header().value();
  const bool append = PROTOBUF_GET_WRAPPED_OR_DEFAULT(header_value_option, append, true);

  std::vector<HeaderFormatterPtr> formatters;

  const absl::string_view format_view(format);
  std::string buf;
  buf.reserve(format_view.size());

  size_t prev = 0;
  do {
    bool flush_buf = false;
    HeaderFormatterPtr formatter;
    size_t pos = format_view.find("%", prev);
    if (pos == absl::string_view::npos) {
      // End of format.
      absl::StrAppend(&buf, format_view.substr(prev));
      prev = format_view.size();
      flush_buf = true;
    } else if (pos + 1 < format_view.size() && format_view[pos + 1] == '%') {
      // Found "%%", copy the first % into the buffer and skip the second.
      absl::StrAppend(&buf, format_view.substr(prev, pos + 1 - prev));
      prev = pos + 2;
      flush_buf = prev >= format_view.size();
    } else {
      // Found the start of a %variable%.
      absl::StrAppend(&buf, format_view.substr(prev, pos - prev));
      flush_buf = true;

      size_t len = std::string::npos;
      absl::string_view var_view = format_view.substr(pos);
      formatter = RequestInfoHeaderFormatter::parse(var_view, append, len);
      if (!formatter) {
        if (len == std::string::npos) {
          throw EnvoyException(fmt::format(
              "Incorrect header configuration. Un-terminated variable expression in '{}'",
              absl::StrCat(var_view)));
        }

        throw EnvoyException(
            fmt::format("Incorrect header configuration. Variable '{}' is not supported",
                        absl::StrCat(var_view.substr(0, len))));
      }

      ASSERT(len != std::string::npos);
      prev = pos + len;
      ASSERT(prev <= format_view.size());
    }

    if (flush_buf && !buf.empty()) {
      formatters.emplace_back(new PlainHeaderFormatter(buf, append));
      buf.clear();
    }

    if (formatter) {
      formatters.push_back(std::move(formatter));
    }
  } while (prev < format_view.size());

  ASSERT(buf.empty());
  ASSERT(formatters.size() > 0);

  if (formatters.size() == 1) {
    return std::move(formatters[0]);
  }

  return HeaderFormatterPtr{new CompoundHeaderFormatter(std::move(formatters), append)};
}

} // namespace

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add) {
  HeaderParserPtr header_parser(new HeaderParser());

  for (const auto& header_value_option : headers_to_add) {
    HeaderFormatterPtr header_formatter = parseInternal(header_value_option);

    header_parser->headers_to_add_.emplace_back(
        Http::LowerCaseString(header_value_option.header().key()), std::move(header_formatter));
  }

  return header_parser;
}

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add,
    const Protobuf::RepeatedPtrField<ProtobufTypes::String>& headers_to_remove) {
  HeaderParserPtr header_parser = configure(headers_to_add);

  for (const auto& header : headers_to_remove) {
    header_parser->headers_to_remove_.emplace_back(header);
  }

  return header_parser;
}

void HeaderParser::evaluateHeaders(Http::HeaderMap& headers,
                                   const RequestInfo::RequestInfo& request_info) const {
  for (const auto& formatter : headers_to_add_) {
    const std::string value = formatter.second->format(request_info);
    if (!value.empty()) {
      if (formatter.second->append()) {
        headers.addReferenceKey(formatter.first, value);
      } else {
        headers.setReferenceKey(formatter.first, value);
      }
    }
  }

  for (const auto& header : headers_to_remove_) {
    headers.remove(header);
  }
}

} // namespace Router
} // namespace Envoy
