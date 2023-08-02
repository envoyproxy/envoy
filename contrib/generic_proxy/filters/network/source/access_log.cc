#include "contrib/generic_proxy/filters/network/source/access_log.h"

#include "envoy/registry/registry.h"

#include "formatter_impl_base.h"
#include "google/protobuf/struct.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class SimpleCommandParser : public GenericProxyCommandParser {
public:
  using SimpleStringFormatterProvider = StringValueFormatterProvider<GenericProxyFormatterContext>;

  using ProviderFunc = std::function<GenericProxyFormatterProviderPtr(
      absl::string_view, absl::optional<size_t> max_length)>;
  using ProviderFuncTable = absl::flat_hash_map<std::string, ProviderFunc>;

  // GenericProxyCommandParser
  GenericProxyFormatterProviderPtr parse(absl::string_view command, absl::string_view command_arg,
                                         absl::optional<size_t> max_length) const override {
    const auto& provider_func_table = providerFuncTable();
    const auto func_iter = provider_func_table.find(std::string(command));
    if (func_iter == provider_func_table.end()) {
      return nullptr;
    }
    return func_iter->second(command_arg, max_length);
  }

private:
  static const ProviderFuncTable& providerFuncTable() {
    CONSTRUCT_ON_FIRST_USE(
        ProviderFuncTable,
        {
            {"METHOD",
             [](absl::string_view, absl::optional<size_t>) -> GenericProxyFormatterProviderPtr {
               return std::make_unique<SimpleStringFormatterProvider>(
                   [](const GenericProxyFormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (context.request_) {
                       return std::string(context.request_->method());
                     }
                     return absl::nullopt;
                   });
             }},
            {"HOST",
             [](absl::string_view, absl::optional<size_t>) -> GenericProxyFormatterProviderPtr {
               return std::make_unique<SimpleStringFormatterProvider>(
                   [](const GenericProxyFormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (context.request_) {
                       return std::string(context.request_->host());
                     }
                     return absl::nullopt;
                   });
             }},
            {"PATH",
             [](absl::string_view, absl::optional<size_t>) -> GenericProxyFormatterProviderPtr {
               return std::make_unique<SimpleStringFormatterProvider>(
                   [](const GenericProxyFormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (context.request_) {
                       return std::string(context.request_->path());
                     }
                     return absl::nullopt;
                   });
             }},
            {"PROTOCOL",
             [](absl::string_view, absl::optional<size_t>) -> GenericProxyFormatterProviderPtr {
               return std::make_unique<SimpleStringFormatterProvider>(
                   [](const GenericProxyFormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (context.request_) {
                       return std::string(context.request_->protocol());
                     }
                     return absl::nullopt;
                   });
             }},
            {"REQUEST_PROPERTY",
             [](absl::string_view command_arg,
                absl::optional<size_t>) -> GenericProxyFormatterProviderPtr {
               return std::make_unique<SimpleStringFormatterProvider>(
                   [key = std::string(command_arg)](
                       const GenericProxyFormatterContext& context,
                       const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (!context.request_) {
                       return absl::nullopt;
                     }
                     auto optional_view = context.request_->getByKey(key);
                     if (!optional_view.has_value()) {
                       return absl::nullopt;
                     }
                     return std::string(optional_view.value());
                   });
             }},
            {"RESPONSE_PROPERTY",
             [](absl::string_view command_arg,
                absl::optional<size_t>) -> GenericProxyFormatterProviderPtr {
               return std::make_unique<SimpleStringFormatterProvider>(
                   [key = std::string(command_arg)](
                       const GenericProxyFormatterContext& context,
                       const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (!context.response_) {
                       return absl::nullopt;
                     }
                     auto optional_view = context.response_->getByKey(key);
                     if (!optional_view.has_value()) {
                       return absl::nullopt;
                     }
                     return std::string(optional_view.value());
                   });
             }},
        });
  }
};

// Regiter the built-in command parsers for the GenericProxyFormatterContext.
REGISTER_BUILT_IN_COMMAND_PARSER(GenericProxyFormatterContext, SimpleCommandParser);

// Register the access log for the GenericProxyFormatterContext.
REGISTER_FACTORY(GenericProxyFileAccessLogFactory, GenericProxyAccessLogInstanceFactory);

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
