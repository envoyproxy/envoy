#pragma once

#include "envoy/api/v2/core/base.pb.h"

namespace Envoy {
namespace Config {

// envoy::api::v2::core::DataSource helper.
class DataSource {
public:
  DataSource(const envoy::api::v2::core::DataSource& source) : source_(source) {}

  /**
   * Read contents of the DataSource.
   * @param allow_empty return an empty string if no DataSource case is specified.
   * @return std::string with DataSource contents.
   * @throw EnvoyException if no DataSource case is specified and !allow_empty.
   */
  std::string read(bool allow_empty) const;

  /**
   * @return std::string path to DataSource if a filename, otherwise an empty string.
   */
  std::string getPath() const;

private:
  const envoy::api::v2::core::DataSource& source_;
};

} // namespace Config
} // namespace Envoy
