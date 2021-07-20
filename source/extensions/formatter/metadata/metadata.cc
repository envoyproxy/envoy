#include "source/extensions/formatter/metadata/metadata.h"

#include <string>

#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/config/metadata.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
#if 0
const ProtobufWkt::Value& unspecifiedValue() { return ValueUtil::nullValue(); }

namespace {

void truncate(std::string& str, absl::optional<uint32_t> max_length) {
  if (!max_length) {
    return;
  }

  str = str.substr(0, max_length.value());
}

} // namespace
#endif


/**
 * 
 */
class RouteMetadataFormatter : public ::Envoy::Formatter::MetadataFormatter {
public:
  RouteMetadataFormatter(const std::string& filter_namespace,
                           const std::vector<std::string>& path, absl::optional<size_t> max_length);
};

RouteMetadataFormatter::RouteMetadataFormatter(const std::string& filter_namespace,
                                                   const std::vector<std::string>& path,
                                                   absl::optional<size_t> max_length)
    : ::Envoy::Formatter::MetadataFormatter(filter_namespace, path, max_length,
                        [](const StreamInfo::StreamInfo& stream_info)
                            -> const envoy::config::core::v3::Metadata* {
                          auto route = stream_info.routeEntry();

                          if (route == nullptr) {
                            return nullptr;
                          }
                          return &route->metadata();
                        }) {}
#if 0
#if 0
MetadataFormatter::MetadataFormatter(const std::string& main_header,
                                 const std::string& alternative_header,
                                 absl::optional<size_t> max_length)
    : main_header_(main_header), alternative_header_(alternative_header), max_length_(max_length) {}
#endif
MetadataFormatter::MetadataFormatter(const std::string&,
                                 const std::string&,
                                 absl::optional<size_t>) {
}

absl::optional<std::string> MetadataFormatter::format(const Http::RequestHeaderMap&,
                                                    const Http::ResponseHeaderMap&,
                                                    const Http::ResponseTrailerMap&,
                                                    const StreamInfo::StreamInfo&,
                                                    absl::string_view) const {
    return absl::nullopt;
#if 0
  const Http::HeaderEntry* header = findHeader(request);
  if (!header) {
    return absl::nullopt;
  }

  std::string val = Http::Utility::stripQueryString(header->value());
  truncate(val, max_length_);

  return val;
#endif
}

ProtobufWkt::Value MetadataFormatter::formatValue(const Http::RequestHeaderMap&,
                                                const Http::ResponseHeaderMap&,
                                                const Http::ResponseTrailerMap&,
                                                const StreamInfo::StreamInfo&,
                                                absl::string_view) const {
    return ValueUtil::nullValue();
#if 0
  const Http::HeaderEntry* header = findHeader(request);
  if (!header) {
    return ValueUtil::nullValue();
  }

  std::string val = Http::Utility::stripQueryString(header->value());
  truncate(val, max_length_);
  return ValueUtil::stringValue(val);
#endif
}
#endif
#if 0
const Http::HeaderEntry* MetadataFormatter::findHeader(const Http::HeaderMap&) const {
    return nullptr;
  const auto header = headers.get(main_header_);

  if (header.empty() && !alternative_header_.get().empty()) {
    const auto alternate_header = headers.get(alternative_header_);
    // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially log all header values.
    return alternate_header.empty() ? nullptr : alternate_header[0];
  }

  return header.empty() ? nullptr : header[0];
}
#endif


MetadataFormatterCommandParser::MetadataFormatterCommandParser() {
  metadata_formatter_providers_["DYNAMIC"] = [] (const std::string& filter_namespace, const std::vector<std::string>& path, absl::optional<size_t> max_length) {return std::make_unique<::Envoy::Formatter::DynamicMetadataFormatter>(filter_namespace, path, max_length);};
   metadata_formatter_providers_["CLUSTER"] = [] (const std::string& filter_namespace, const std::vector<std::string>& path, absl::optional<size_t> max_length) { return std::make_unique<::Envoy::Formatter::ClusterMetadataFormatter>(filter_namespace, path, max_length);};
    metadata_formatter_providers_["ROUTE"] = [] (const std::string& filter_namespace, const std::vector<std::string>& path, absl::optional<size_t> max_length) { return std::make_unique<RouteMetadataFormatter>(filter_namespace, path, max_length);};
}

::Envoy::Formatter::FormatterProviderPtr
MetadataFormatterCommandParser::parse(const std::string& token, size_t, size_t) const {
    const std::string METADATA_TOKEN = "METADATA("; 
    if(absl::StartsWith(token, METADATA_TOKEN)) {
        // extract type of metadata and keys
       std::string type, param1;
    absl::optional<size_t> max_length;
    std::vector<std::string> path;
    const size_t start = METADATA_TOKEN.size();

    ::Envoy::Formatter::SubstitutionFormatParser::parseCommand(token, start, ':', max_length, type, param1, path);

    auto provider = metadata_formatter_providers_.find(type);
    if (provider == metadata_formatter_providers_.end()){
  return nullptr;
} 
    return provider->second(param1, path, max_length);
#if 0
    if (type == "DYNAMIC") {
      // Dynamic metadata needs at least the filter_namespace and the key.
      return std::make_unique<::Envoy::Formatter::DynamicMetadataFormatter>(param1, path, max_length);
    } else if (type == "CLUSTER") {
      return std::make_unique<::Envoy::Formatter::ClusterMetadataFormatter>(param1, path, max_length);
    } else if (type == "ROUTE") {
      return std::make_unique<RouteMetadataFormatter>(param1, path, max_length);
    } else {
        ASSERT(false);
    }
#endif
    }
  return nullptr;

}


} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
