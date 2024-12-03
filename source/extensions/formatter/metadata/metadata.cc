#include "source/extensions/formatter/metadata/metadata.h"

#include <string>

#include "source/common/config/metadata.h"
#include "source/common/formatter/stream_info_formatter.h"
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
  RouteMetadataFormatter(absl::string_view filter_namespace,
                         const std::vector<absl::string_view>& path,
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

// Metadata formatter for listener metadata.
class ListenerMetadataFormatter : public ::Envoy::Formatter::MetadataFormatter {
public:
  ListenerMetadataFormatter(absl::string_view filter_namespace,
                            const std::vector<absl::string_view>& path,
                            absl::optional<size_t> max_length)
      : ::Envoy::Formatter::MetadataFormatter(
            filter_namespace, path, max_length,
            [](const StreamInfo::StreamInfo& stream_info)
                -> const envoy::config::core::v3::Metadata* {
              const auto listener_info = stream_info.downstreamAddressProvider().listenerInfo();
              if (listener_info) {
                return &listener_info->metadata();
              }
              return nullptr;
            }) {}
};

// Metadata formatter for virtual host metadata.
class VirtualHostMetadataFormatter : public ::Envoy::Formatter::MetadataFormatter {
public:
  VirtualHostMetadataFormatter(absl::string_view filter_namespace,
                               const std::vector<absl::string_view>& path,
                               absl::optional<size_t> max_length)
      : ::Envoy::Formatter::MetadataFormatter(filter_namespace, path, max_length,
                                              [](const StreamInfo::StreamInfo& stream_info)
                                                  -> const envoy::config::core::v3::Metadata* {
                                                Router::RouteConstSharedPtr route =
                                                    stream_info.route();
                                                if (route == nullptr) {
                                                  return nullptr;
                                                }
                                                return &route->virtualHost().metadata();
                                              }) {}
};

// Map used to dispatch types of metadata to individual handlers which will
// access required metadata object.
using FormatterProviderFunc = std::function<::Envoy::Formatter::StreamInfoFormatterProviderPtr(
    absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
    absl::optional<size_t> max_length)>;

using FormatterProviderFuncTable = absl::flat_hash_map<std::string, FormatterProviderFunc>;

const auto& formatterProviderFuncTable() {
  CONSTRUCT_ON_FIRST_USE(
      FormatterProviderFuncTable,
      {
          {"DYNAMIC",
           [](absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
              absl::optional<size_t> max_length) {
             return std::make_unique<::Envoy::Formatter::DynamicMetadataFormatter>(
                 filter_namespace, path, max_length);
           }},
          {"CLUSTER",
           [](absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
              absl::optional<size_t> max_length) {
             return std::make_unique<::Envoy::Formatter::ClusterMetadataFormatter>(
                 filter_namespace, path, max_length);
           }},
          {"ROUTE",
           [](absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
              absl::optional<size_t> max_length) {
             return std::make_unique<RouteMetadataFormatter>(filter_namespace, path, max_length);
           }},
          {"UPSTREAM_HOST",
           [](absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
              absl::optional<size_t> max_length) {
             return std::make_unique<::Envoy::Formatter::UpstreamHostMetadataFormatter>(
                 filter_namespace, path, max_length);
           }},
          {"LISTENER",
           [](absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
              absl::optional<size_t> max_length) {
             return std::make_unique<ListenerMetadataFormatter>(filter_namespace, path, max_length);
           }},
          {"VIRTUAL_HOST",
           [](absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
              absl::optional<size_t> max_length) {
             return std::make_unique<VirtualHostMetadataFormatter>(filter_namespace, path,
                                                                   max_length);
           }},
      });
}

::Envoy::Formatter::FormatterProviderPtr
MetadataFormatterCommandParser::parse(absl::string_view command, absl::string_view subcommand,
                                      absl::optional<size_t> max_length) const {
  if (command == "METADATA") {
    // Extract type of metadata and keys.
    absl::string_view type, filter_namespace;
    std::vector<absl::string_view> path;

    ::Envoy::Formatter::SubstitutionFormatUtils::parseSubcommand(subcommand, ':', type,
                                                                 filter_namespace, path);

    auto provider = formatterProviderFuncTable().find(type);
    if (provider == formatterProviderFuncTable().end()) {
      throw EnvoyException(absl::StrCat(type, " is not supported type of metadata"));
    }

    // Return a pointer to formatter provider.
    return std::make_unique<
        Envoy::Formatter::StreamInfoFormatterWrapper<Envoy::Formatter::HttpFormatterContext>>(
        provider->second(filter_namespace, path, max_length));
  }
  return nullptr;
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
