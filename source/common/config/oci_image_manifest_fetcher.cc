#include "source/common/config/oci_image_manifest_fetcher.h"

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"

#include "remote_data_fetcher.h"

namespace Envoy {
namespace Config {
namespace DataFetcher {

OciImageManifestFetcher::OciImageManifestFetcher(Upstream::ClusterManager& cm,
                                                 const envoy::config::core::v3::HttpUri& uri,
                                                 const std::string& content_hash,
                                                 RemoteDataFetcherCallback& callback,
                                                 const std::string& authz_header_value)
    : RemoteDataFetcher(cm, uri, content_hash, callback), authz_header_value_(authz_header_value) {}

void OciImageManifestFetcher::fetch() {
  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(uri_);
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
  message->headers().setAuthorization(authz_header_value_);

  // TODO: set "Accept: application/vnd.oci.image.manifest.v1+json"
  ENVOY_LOG(info, "fetch oci image from [uri = {}]: start", uri_.uri());
  const auto thread_local_cluster = cm_.getThreadLocalCluster(uri_.cluster());
  if (thread_local_cluster != nullptr) {
    request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(message), *this,
        Http::AsyncClient::RequestOptions().setTimeout(
            std::chrono::milliseconds(DurationUtil::durationToMilliseconds(uri_.timeout()))));
  } else {
    ENVOY_LOG(info, "fetch oci image [uri = {}]: no cluster {}", uri_.uri(), uri_.cluster());
    callback_.onFailure(FailureReason::Network);
  }
}

void OciImageManifestFetcher::onSuccess(const Http::AsyncClient::Request&,
                                        Http::ResponseMessagePtr&& response) {
  const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code != enumToInt(Http::Code::OK)) {
    onInvalidData(fmt::format("failed to fetch oci image [uri = {}, status code {}, body = {}]",
                              uri_.uri(), response->body().toString(), status_code));
    return;
  }

  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto content_hash = Hex::encode(crypto_util.getSha256Digest(response->body()));
  if (content_hash_ != content_hash) {
    onInvalidData(
        fmt::format("failed to verify content hash [uri = {}, body = {}, content hash = {}]",
                    uri_.uri(), response->body().toString(), content_hash));
    return;
  }

  auto json_body = Json::Factory::loadFromString(response->bodyAsString());
  if (!json_body.ok()) {
    onInvalidData(fmt::format("failed to parse OCI manifest [uri = {}, response body = {}]: {}",
                              uri_.uri(), response->body().toString(),
                              json_body.status().message()));
    return;
  }

  auto layers = json_body.value()->getObjectArray("layers");
  if (!layers.ok()) {
    onInvalidData(fmt::format(
        "failed to parse 'layers' in the received manifest [uri = {}, response body = {}]: {}",
        uri_.uri(), response->body().toString(), layers.status().message()));
    return;
  } else if (layers.value().empty()) {
    onInvalidData(
        fmt::format("received a manifest with empty layers [uri = {}, response body = {}]: {}",
                    uri_.uri(), response->body().toString(), layers.status().message()));
    return;
  }

  auto digest = layers.value()[0]->getString("digest", "");
  if (!digest.ok()) {
    onInvalidData(fmt::format("failed to parse 'layers[0].digest' in the received manifest [uri = "
                              "{}, response body = {}]: {}",
                              uri_.uri(), response->body().toString(), digest.status().message()));
    return;
  } else if (digest.value().empty()) {
    onInvalidData(
        fmt::format("received a manifest with empty digest [uri = {}, response body = {}]: {}",
                    uri_.uri(), response->body().toString(), digest.status().message()));
    return;
  }

  callback_.onSuccess(digest.value());
  request_ = nullptr;
}

void OciImageManifestFetcher::onInvalidData(std::string error_message) {
  ENVOY_LOG(error, error_message);
  callback_.onFailure(FailureReason::InvalidData);
  request_ = nullptr;
}

} // namespace DataFetcher
} // namespace Config
} // namespace Envoy
