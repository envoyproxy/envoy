#pragma once

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/extensions/common/wasm/remote_async_datasource.h"

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
