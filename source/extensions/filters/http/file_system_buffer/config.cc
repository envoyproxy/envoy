#include "source/extensions/filters/http/file_system_buffer/config.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "source/extensions/filters/http/file_system_buffer/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

using Extensions::Common::AsyncFiles::AsyncFileManager;
using Extensions::Common::AsyncFiles::AsyncFileManagerFactory;

FileSystemBufferFilterFactory::FileSystemBufferFilterFactory()
    : FactoryBase(FileSystemBufferFilter::filterName()) {}

Http::FilterFactoryCb FileSystemBufferFilterFactory::createFilterFactoryFromProtoTyped(
    const ProtoFileSystemBufferFilterConfig& config,
    const std::string& stats_prefix ABSL_ATTRIBUTE_UNUSED,
    Server::Configuration::FactoryContext& context) {
  auto factory =
      AsyncFileManagerFactory::singleton(&context.serverFactoryContext().singletonManager());
  auto manager = config.has_manager_config() ? factory->getAsyncFileManager(config.manager_config())
                                             : std::shared_ptr<AsyncFileManager>();
  auto filter_config = std::make_shared<FileSystemBufferFilterConfig>(std::move(factory),
                                                                      std::move(manager), config);
  return [filter_config =
              std::move(filter_config)](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<FileSystemBufferFilter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
FileSystemBufferFilterFactory::createRouteSpecificFilterConfigTyped(
    const ProtoFileSystemBufferFilterConfig& config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  auto factory = AsyncFileManagerFactory::singleton(&context.singletonManager());
  auto manager = config.has_manager_config() ? factory->getAsyncFileManager(config.manager_config())
                                             : std::shared_ptr<AsyncFileManager>();
  return std::make_shared<FileSystemBufferFilterConfig>(std::move(factory), std::move(manager),
                                                        config);
}

REGISTER_FACTORY(FileSystemBufferFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
