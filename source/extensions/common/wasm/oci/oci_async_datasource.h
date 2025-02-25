#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/init/manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/init/target_impl.h"
#include "source/extensions/common/wasm/oci/oci_image_blob_fetcher.h"
#include "source/extensions/common/wasm/oci/oci_image_manifest_fetcher.h"
#include "source/extensions/common/wasm/remote_async_datasource.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

class OciManifestProvider : public RemoteAsyncDataProvider {
public:
  OciManifestProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                      const envoy::config::core::v3::RemoteDataSource& source,
                      Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                      const envoy::config::core::v3::HttpUri uri, std::string token,
                      std::string sha256, bool allow_empty, AsyncDataSourceCb&& callback);
};

using OciManifestProviderPtr = std::unique_ptr<OciManifestProvider>;

class OciBlobProvider : public RemoteAsyncDataProvider {
public:
  OciBlobProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                  const envoy::config::core::v3::RemoteDataSource& source,
                  Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                  const envoy::config::core::v3::HttpUri uri, std::string token, std::string digest,
                  std::string sha256, bool allow_empty, AsyncDataSourceCb&& callback);
};

using OciBlobProviderPtr = std::unique_ptr<OciBlobProvider>;

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
