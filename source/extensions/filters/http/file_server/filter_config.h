#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/file_server/v3/file_server.pb.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/radix_tree.h"
#include "source/extensions/common/async_files/async_file_manager.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

using ProtoFileServerConfig = envoy::extensions::filters::http::file_server::v3::FileServerConfig;
using ::Envoy::Extensions::Common::AsyncFiles::AsyncFileManager;

class FileServerConfig : public Router::RouteSpecificFilterConfig {
public:
  static absl::StatusOr<std::shared_ptr<const FileServerConfig>>
  create(const ProtoFileServerConfig& config,
         Envoy::Server::Configuration::ServerFactoryContext& context);
  FileServerConfig(const ProtoFileServerConfig& config, std::shared_ptr<AsyncFileManager> manager);

  const std::shared_ptr<AsyncFileManager>& asyncFileManager() const { return async_file_manager_; }
  // Returns nullptr if there is no corresponding path mapping (filter should be bypassed).
  std::shared_ptr<const ProtoFileServerConfig::PathMapping>
  pathMapping(absl::string_view path) const;
  // Returns nullopt if the resulting path is not lexically normalized,
  // e.g. foo/./bar rather than foo/bar, or foo/../bar rather than bar.
  static absl::optional<std::filesystem::path>
  applyPathMapping(absl::string_view path, const ProtoFileServerConfig::PathMapping& mapping);

  absl::string_view contentTypeForPath(const std::filesystem::path& path) const;
  // nullopt if out of behaviors.
  OptRef<const ProtoFileServerConfig::DirectoryBehavior> directoryBehavior(size_t index) const;

private:
  const std::shared_ptr<AsyncFileManager> async_file_manager_;
  const RadixTree<std::shared_ptr<const ProtoFileServerConfig::PathMapping>> path_mappings_;
  const absl::flat_hash_map<std::string, std::string> content_types_;
  const std::string default_content_type_;
  const std::vector<const ProtoFileServerConfig::DirectoryBehavior> directory_behaviors_;
};

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
