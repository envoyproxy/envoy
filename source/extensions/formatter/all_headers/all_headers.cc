#include "source/extensions/formatter/all_headers/all_headers.h"

#include <string>

#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

AllHeadersFormatter::AllHeadersFormatter(AllHeadersType type, uint32_t max_value_bytes,
                                         absl::flat_hash_set<std::string> exclude_headers)
    : type_(type), max_value_bytes_(max_value_bytes), exclude_headers_(std::move(exclude_headers)) {
}

Protobuf::Struct AllHeadersFormatter::buildHeadersStruct(const Http::HeaderMap& headers) const {
  Protobuf::Struct headers_struct;
  auto* fields = headers_struct.mutable_fields();

  headers.iterate([&](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
    const std::string key_lower = absl::AsciiStrToLower(entry.key().getStringView());
    if (exclude_headers_.contains(key_lower)) {
      return Http::HeaderMap::Iterate::Continue;
    }

    absl::string_view val = entry.value().getStringView();
    if (max_value_bytes_ > 0 && val.size() > max_value_bytes_) {
      val = val.substr(0, max_value_bytes_);
    }

    // Comma-join repeated headers per RFC 7230.
    auto it = fields->find(key_lower);
    if (it != fields->end()) {
      it->second.set_string_value(absl::StrCat(it->second.string_value(), ",", val));
    } else {
      (*fields)[key_lower].set_string_value(std::string(val));
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  return headers_struct;
}

namespace {
const Http::HeaderMap* getHeaders(AllHeadersType type, const Envoy::Formatter::Context& context) {
  if (type == AllHeadersType::Request) {
    const auto ref = context.requestHeaders();
    return ref.has_value() ? &ref.ref() : nullptr;
  }
  const auto ref = context.responseHeaders();
  return ref.has_value() ? &ref.ref() : nullptr;
}
} // namespace

absl::optional<std::string> AllHeadersFormatter::format(const Envoy::Formatter::Context& context,
                                                        const StreamInfo::StreamInfo&) const {
  const Http::HeaderMap* headers = getHeaders(type_, context);
  if (headers == nullptr) {
    return absl::nullopt;
  }

  Protobuf::Struct headers_struct = buildHeadersStruct(*headers);
  Protobuf::Value wrapper;
  *wrapper.mutable_struct_value() = std::move(headers_struct);
  return MessageUtil::getJsonStringFromMessageOrError(wrapper, false, true);
}

Protobuf::Value AllHeadersFormatter::formatValue(const Envoy::Formatter::Context& context,
                                                 const StreamInfo::StreamInfo&) const {
  const Http::HeaderMap* headers = getHeaders(type_, context);
  if (headers == nullptr) {
    return ValueUtil::nullValue();
  }

  return ValueUtil::structValue(buildHeadersStruct(*headers));
}

AllHeadersCommandParser::AllHeadersCommandParser(uint32_t max_value_bytes,
                                                 absl::flat_hash_set<std::string> exclude_headers)
    : max_value_bytes_(max_value_bytes), exclude_headers_(std::move(exclude_headers)) {}

::Envoy::Formatter::FormatterProviderPtr
AllHeadersCommandParser::parse(absl::string_view command, absl::string_view subcommand,
                               absl::optional<size_t> max_length) const {
  AllHeadersType type;
  if (command == "REQ_ALL_HEADERS") {
    type = AllHeadersType::Request;
  } else if (command == "RESP_ALL_HEADERS") {
    type = AllHeadersType::Response;
  } else {
    return nullptr;
  }

  if (!subcommand.empty()) {
    throw EnvoyException(
        fmt::format("{} does not accept subcommands, got '{}'", command, subcommand));
  }
  if (max_length.has_value()) {
    throw EnvoyException(fmt::format("{} does not accept a max_length parameter", command));
  }

  return std::make_unique<AllHeadersFormatter>(type, max_value_bytes_, exclude_headers_);
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
