#pragma once

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/init/manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/remote_data_fetcher.h"
#include "source/common/init/target_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {
namespace DataSource {

/**
 * Read contents of the DataSource.
 * @param source data source.
 * @param allow_empty return an empty string if no DataSource case is specified.
 * @param api reference to the Api object
 * @param max_size max size limit of file to read, default 0 means no limit, and if the file data
 * would exceed the limit, it will throw a EnvoyException.
 * @return std::string with DataSource contents. or an error status if no DataSource case is
 * specified and !allow_empty.
 */
absl::StatusOr<std::string> read(const envoy::config::core::v3::DataSource& source,
                                 bool allow_empty, Api::Api& api, uint64_t max_size = 0);

/**
 * @param source data source.
 * @return absl::optional<std::string> path to DataSource if a filename, otherwise absl::nullopt.
 */
absl::optional<std::string> getPath(const envoy::config::core::v3::DataSource& source);

} // namespace DataSource
} // namespace Config
} // namespace Envoy
