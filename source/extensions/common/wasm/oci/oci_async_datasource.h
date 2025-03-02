#pragma once

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/extensions/common/wasm/remote_async_datasource.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

class ManifestProvider : public RemoteAsyncDataProvider {
public:
  ManifestProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                   const envoy::config::core::v3::RemoteDataSource& source,
                   Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                   const envoy::config::core::v3::HttpUri& uri, const std::string& credential,
                   bool allow_empty, AsyncDataSourceCb&& callback);
};

using ManifestProviderPtr = std::unique_ptr<ManifestProvider>;

class BlobProvider : public RemoteAsyncDataProvider {
public:
  BlobProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
               const envoy::config::core::v3::RemoteDataSource& source,
               Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
               const envoy::config::core::v3::HttpUri& uri, const std::string& credential,
               const std::string& sha256, bool allow_empty, AsyncDataSourceCb&& callback);
};

using BlobProviderPtr = std::unique_ptr<BlobProvider>;

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
