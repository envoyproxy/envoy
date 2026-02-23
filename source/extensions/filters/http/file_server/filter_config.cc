#include "source/extensions/filters/http/file_server/filter_config.h"

#include <utility>

#include "envoy/common/exception.h"

#include "source/common/common/thread.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

namespace {

RadixTree<std::shared_ptr<const ProtoFileServerConfig::PathMapping>>
makePathMappings(const ProtoFileServerConfig& config) {
  RadixTree<std::shared_ptr<const ProtoFileServerConfig::PathMapping>> tree;
  for (const auto& mapping : config.path_mappings()) {
    tree.add(mapping.request_path_prefix(),
             std::make_shared<ProtoFileServerConfig::PathMapping>(mapping));
  }
  return tree;
}

} // namespace

absl::StatusOr<std::shared_ptr<const FileServerConfig>>
FileServerConfig::create(const ProtoFileServerConfig& config,
                         Envoy::Server::Configuration::ServerFactoryContext& context) {
  auto factory = AsyncFileManagerFactory::singleton(&context.singletonManager());
  TRY_ASSERT_MAIN_THREAD {
    // TODO(ravenblack): make getAsyncFileManager use StatusOr instead of throw.
    auto async_file_manager = factory->getAsyncFileManager(config.manager_config());
    return std::make_shared<const FileServerConfig>(config, std::move(factory),
                                                    std::move(async_file_manager));
  }
  END_TRY
  catch (const EnvoyException& e) {
    return absl::InvalidArgumentError(e.what());
  }
}

FileServerConfig::FileServerConfig(const ProtoFileServerConfig& config,
                                   std::shared_ptr<AsyncFileManagerFactory> factory,
                                   std::shared_ptr<AsyncFileManager> manager)
    : async_file_manager_factory_(std::move(factory)), async_file_manager_(std::move(manager)),
      path_mappings_(makePathMappings(config)),
      content_types_(config.content_types().begin(), config.content_types().end()),
      default_content_type_(config.default_content_type()),
      directory_behaviors_(config.directory_behaviors().begin(),
                           config.directory_behaviors().end()) {}

std::shared_ptr<const ProtoFileServerConfig::PathMapping>
FileServerConfig::pathMapping(absl::string_view path) const {
  return path_mappings_.findLongestPrefix(path);
}

absl::optional<std::filesystem::path>
FileServerConfig::applyPathMapping(absl::string_view path_with_query,
                                   const ProtoFileServerConfig::PathMapping& mapping) {
  std::pair<absl::string_view, absl::string_view> split = absl::StrSplit(path_with_query, '?');
  absl::string_view kept_path = split.first.substr(mapping.request_path_prefix().length());
  if (kept_path.starts_with('/')) {
    if (mapping.request_path_prefix().ends_with('/')) {
      // Avoid accepting a value that parses away a double-slash at the join-point.
      // (Other double-slashes will be rejected by the lexically_normal check.)
      return absl::nullopt;
    }
    // filesystem::path operator / treats the second operand starting with a / as
    // meaning replace the entire path, and we don't want to do that.
    kept_path.remove_prefix(1);
  }
  if (kept_path.starts_with('/')) {
    // We don't want to remove more than one slash, to avoid foo/bar, foo//bar
    // and foo///bar all acting valid.
    return absl::nullopt;
  }
  std::filesystem::path file_path =
      std::filesystem::path{mapping.file_path_prefix()} / std::filesystem::path{kept_path};
  if (file_path != file_path.lexically_normal() ||
      !file_path.string().starts_with(mapping.file_path_prefix())) {
    // Ensure we're not accidentally looking outside the designated filesystem prefix
    // in any way controlled by the client. (Symlink escapes are up to the filesystem owner.)
    return absl::nullopt;
  }
  return file_path;
}

absl::string_view FileServerConfig::contentTypeForPath(const std::filesystem::path& path) const {
  std::string suffix = path.extension();
  if (suffix.empty()) {
    // For files with no suffix, use the whole filename.
    suffix = path.stem();
  } else {
    // Remove the dot.
    suffix = suffix.substr(1);
  }
  auto it = content_types_.find(suffix);
  if (it == content_types_.end()) {
    return default_content_type_;
  }
  return it->second;
}

OptRef<const ProtoFileServerConfig::DirectoryBehavior>
FileServerConfig::directoryBehavior(size_t index) const {
  if (index >= directory_behaviors_.size()) {
    return absl::nullopt;
  }
  return directory_behaviors_[index];
}

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
