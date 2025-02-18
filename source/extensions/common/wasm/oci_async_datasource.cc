#include "source/extensions/common/wasm/oci_async_datasource.h"

#include <string>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/utility.h"

#include "fmt/format.h"

namespace Envoy {

OciManifestProvider::OciManifestProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                                         const envoy::config::core::v3::HttpUri uri,
                                         std::string token, std::string sha256, bool allow_empty,
                                         OciManifestCb&& callback)
    : allow_empty_(allow_empty), callback_(std::move(callback)),
      fetcher_(std::make_unique<Config::DataFetcher::OciImageManifestFetcher>(cm, uri, sha256,
                                                                              *this, token)),
      init_target_("OciManifestProvider", [this]() { start(); }) {

  manager.add(init_target_);
}

OciBlobProvider::OciBlobProvider(Upstream::ClusterManager& cm, Init::Manager& manager,
                                 const envoy::config::core::v3::HttpUri uri,
                                 std::string authz_header_value, std::string digest,
                                 std::string sha256, bool allow_empty, OciBlobCb&& callback)
    : allow_empty_(allow_empty), callback_(std::move(callback)),
      fetcher_(std::make_unique<Config::DataFetcher::OciImageBlobFetcher>(
          cm, uri, sha256, *this, authz_header_value, digest)),
      init_target_("OciBlobProvider", [this]() { start(); }) {

  manager.add(init_target_);
}

} // namespace Envoy
