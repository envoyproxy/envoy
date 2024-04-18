#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/file_system_buffer/v3/file_system_buffer.pb.h"
#include "envoy/extensions/filters/http/file_system_buffer/v3/file_system_buffer.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/common/async_files/async_file_manager_factory.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

// Config registration for the file system buffer filter. @see NamedHttpFilterConfigFactory.
class FileSystemBufferFilterFactory
    : public Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::file_system_buffer::v3::FileSystemBufferFilterConfig> {
public:
  FileSystemBufferFilterFactory();

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::file_system_buffer::v3::FileSystemBufferFilterConfig&
          config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::file_system_buffer::v3::FileSystemBufferFilterConfig&
          config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
