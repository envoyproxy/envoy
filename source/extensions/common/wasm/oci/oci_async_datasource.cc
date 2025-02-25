#include "source/extensions/common/wasm/oci/oci_async_datasource.h"

#include "source/extensions/common/wasm/oci/oci_image_blob_fetcher.h"
#include "source/extensions/common/wasm/oci/oci_image_manifest_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

ManifestProvider::ManifestProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                                   const envoy::config::core::v3::RemoteDataSource& source,
                                   Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                                   const envoy::config::core::v3::HttpUri uri, std::string token,
                                   std::string sha256, bool allow_empty,
                                   AsyncDataSourceCb&& callback)
    : RemoteAsyncDataProvider(
          [this, &cm, uri, sha256, token]() {
            return std::make_unique<Extensions::Common::Wasm::Oci::ImageManifestFetcher>(
                cm, uri, sha256, *this, token);
          },
          manager, source, dispatcher, random, allow_empty, std::move(callback)){};

BlobProvider::BlobProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                           const envoy::config::core::v3::RemoteDataSource& source,
                           Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                           const envoy::config::core::v3::HttpUri uri,
                           std::string authz_header_value, std::string digest, std::string sha256,
                           bool allow_empty, AsyncDataSourceCb&& callback)
    : RemoteAsyncDataProvider(
          [this, &cm, uri, sha256, authz_header_value, digest]() {
            return std::make_unique<Extensions::Common::Wasm::Oci::ImageBlobFetcher>(
                cm, uri, sha256, *this, authz_header_value, digest);
          },
          manager, source, dispatcher, random, allow_empty, std::move(callback)){};

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
