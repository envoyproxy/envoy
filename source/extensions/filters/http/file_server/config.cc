#include "source/extensions/filters/http/file_server/config.h"

#include <memory>
#include <string>
#include <utility>

#include "envoy/extensions/filters/http/file_server/v3/file_server.pb.validate.h"

#include "source/extensions/filters/http/file_server/filter.h"
#include "source/extensions/filters/http/file_server/filter_config.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

namespace {
absl::Status validateProto(const ProtoFileServerConfig& config) {
  absl::flat_hash_set<absl::string_view> seen;
  for (const auto& mapping : config.path_mappings()) {
    auto [_, inserted] = seen.emplace(mapping.request_path_prefix());
    if (!inserted) {
      return absl::InvalidArgumentError(
          absl::StrCat("duplicate request_path_prefix: ", mapping.request_path_prefix()));
    }
  }
  seen.clear();
  bool directory_tried = false;
  static const absl::string_view directory_options = "default_file or list";
  for (const auto& directory_behavior : config.directory_behaviors()) {
    if (directory_behavior.default_file().empty() && !directory_behavior.has_list()) {
      return absl::InvalidArgumentError(
          absl::StrCat("directory_behavior must set one of ", directory_options));
    }
    if (!directory_behavior.default_file().empty() && directory_behavior.has_list()) {
      return absl::InvalidArgumentError(
          absl::StrCat("directory_behavior must have only one of ", directory_options));
    }
    if (!directory_behavior.default_file().empty()) {
      auto [_, inserted] = seen.emplace(directory_behavior.default_file());
      if (!inserted) {
        return absl::InvalidArgumentError(absl::StrCat(
            "duplicate default_file in directory_behaviors: ", directory_behavior.default_file()));
      }
    } else {
      if (directory_tried) {
        return absl::InvalidArgumentError("multiple list directives");
      }
      directory_tried = true;
    }
  }
  for (const auto& content_type_pair : config.content_types()) {
    if (content_type_pair.first.find(".") != std::string::npos) {
      return absl::InvalidArgumentError(absl::StrCat(
          "file suffix in content_types may not contain a period: ", content_type_pair.first));
    }
  }
  return absl::OkStatus();
}
} // namespace

FileServerFilterFactory::FileServerFilterFactory()
    : DualFactoryBase(FileServerFilter::filterName()) {}

absl::StatusOr<Http::FilterFactoryCb> FileServerFilterFactory::createFilterFactoryFromProtoTyped(
    const ProtoFileServerConfig& config, const std::string&, DualInfo,
    Server::Configuration::ServerFactoryContext& context) {
  RETURN_IF_NOT_OK(validateProto(config));
  auto file_server_config = FileServerConfig::create(config, context);
  if (!file_server_config.ok()) {
    return file_server_config.status();
  }
  return [fsc = std::move(file_server_config.value())](
             Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_unique<FileServerFilter>(fsc));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
FileServerFilterFactory::createRouteSpecificFilterConfigTyped(
    const ProtoFileServerConfig& config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {
  RETURN_IF_NOT_OK(validateProto(config));
  auto file_server_config = FileServerConfig::create(config, context);
  if (!file_server_config.ok()) {
    return file_server_config.status();
  }
  return file_server_config.value();
}

REGISTER_FACTORY(FileServerFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
