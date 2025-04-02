#include "source/extensions/common/wasm/oci/oci_async_datasource.h"

#include "source/extensions/common/wasm/oci/oci_image_blob_fetcher.h"
#include "source/extensions/common/wasm/oci/oci_image_manifest_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

ManifestProvider::ManifestProvider(
    Upstream::ClusterManager& cm, Init::Manager& manager,
    const envoy::config::core::v3::RemoteDataSource& source, Event::Dispatcher& dispatcher,
    Random::RandomGenerator& random, const envoy::config::core::v3::HttpUri& uri,
    std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> image_pull_secret_provider,
    const std::string registry, bool allow_empty, AsyncDataSourceCb&& callback)
    : RemoteAsyncDataProvider(
          [this, &cm, &uri, &image_pull_secret_provider, &registry]() {
            return std::make_unique<Extensions::Common::Wasm::Oci::ImageManifestFetcher>(
                cm, uri, *this, image_pull_secret_provider, registry);
          },
          "ManifestProvider", manager, source, dispatcher, random, allow_empty,
          std::move(callback)) {};

BlobProvider::BlobProvider(
    Upstream::ClusterManager& cm, Init::Manager& manager,
    const envoy::config::core::v3::RemoteDataSource& source, Event::Dispatcher& dispatcher,
    Random::RandomGenerator& random, const envoy::config::core::v3::HttpUri& uri,
    std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> image_pull_secret_provider,
    const std::string& registry, const std::string& sha256, bool allow_empty,
    AsyncDataSourceCb&& callback)
    : RemoteAsyncDataProvider(
          [this, &cm, &uri, &sha256, &image_pull_secret_provider, &registry]() {
            return std::make_unique<Extensions::Common::Wasm::Oci::ImageBlobFetcher>(
                cm, uri, sha256, *this, image_pull_secret_provider, registry);
          },
          "BlobProvider", manager, source, dispatcher, random, allow_empty, std::move(callback)) {};

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
