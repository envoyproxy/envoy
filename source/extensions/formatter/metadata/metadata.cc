#include "source/extensions/formatter/metadata/metadata.h"

#include <string>

#include "source/common/config/metadata.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

// Metadata formatter for route's metadata.
class RouteMetadataFormatter : public ::Envoy::Formatter::MetadataFormatter {
public:
  RouteMetadataFormatter(const std::string& filter_namespace, const std::vector<std::string>& path,
                         absl::optional<size_t> max_length)
      : ::Envoy::Formatter::MetadataFormatter(filter_namespace, path, max_length,
                                              [](const StreamInfo::StreamInfo& stream_info)
                                                  -> const envoy::config::core::v3::Metadata* {
                                                auto route = stream_info.route();

                                                if (route == nullptr) {
                                                  return nullptr;
                                                }
                                                return &route->metadata();
                                              }) {}
};

// Constructor registers all types of supported metadata along with the
// handlers accessing the required metadata type.
MetadataFormatterCommandParser::MetadataFormatterCommandParser() {
  metadata_formatter_providers_["DYNAMIC"] = [](const std::string& filter_namespace,
                                                const std::vector<std::string>& path,
                                                absl::optional<size_t> max_length) {
    return std::make_unique<::Envoy::Formatter::DynamicMetadataFormatter>(filter_namespace, path,
                                                                          max_length);
  };
  metadata_formatter_providers_["CLUSTER"] = [](const std::string& filter_namespace,
                                                const std::vector<std::string>& path,
                                                absl::optional<size_t> max_length) {
    return std::make_unique<::Envoy::Formatter::ClusterMetadataFormatter>(filter_namespace, path,
                                                                          max_length);
  };
  metadata_formatter_providers_["ROUTE"] = [](const std::string& filter_namespace,
                                              const std::vector<std::string>& path,
                                              absl::optional<size_t> max_length) {
    return std::make_unique<RouteMetadataFormatter>(filter_namespace, path, max_length);
  };
}

::Envoy::Formatter::FormatterProviderPtr
MetadataFormatterCommandParser::parse(const std::string& token, size_t, size_t) const {
  constexpr absl::string_view METADATA_TOKEN = "METADATA(";
  if (absl::StartsWith(token, METADATA_TOKEN)) {
    // Extract type of metadata and keys.
    std::string type, filter_namespace;
    absl::optional<size_t> max_length;
    std::vector<std::string> path;
    const size_t start = METADATA_TOKEN.size();

    ::Envoy::Formatter::SubstitutionFormatParser::parseCommand(token, start, ':', max_length, type,
                                                               filter_namespace, path);

    auto provider = metadata_formatter_providers_.find(type);
    if (provider == metadata_formatter_providers_.end()) {
      throw EnvoyException(absl::StrCat(type, " is not supported type of metadata"));
    }

    // Return a pointer to formatter provider.
    return provider->second(filter_namespace, path, max_length);
  }
  return nullptr;
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
