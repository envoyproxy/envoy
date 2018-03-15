#pragma once

#include "envoy/api/v2/core/base.pb.h"

namespace Envoy {
namespace Config {
namespace DataSource {

/**
 * Read contents of the DataSource.
 * @param source data source.
 * @param allow_empty return an empty string if no DataSource case is specified.
 * @return std::string with DataSource contents.
 * @throw EnvoyException if no DataSource case is specified and !allow_empty.
 */
std::string read(const envoy::api::v2::core::DataSource& source, bool allow_empty);

/**
 * @param source data source.
 * @return std::string path to DataSource if a filename, otherwise an empty string.
 */
std::string getPath(const envoy::api::v2::core::DataSource& source);

} // namespace DataSource
} // namespace Config
} // namespace Envoy
