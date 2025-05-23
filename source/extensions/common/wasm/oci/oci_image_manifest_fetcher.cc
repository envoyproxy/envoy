#include "source/extensions/common/wasm/oci/oci_image_manifest_fetcher.h"

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/common/wasm/oci/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

ImageManifestFetcher::ImageManifestFetcher(
    Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
    Config::DataFetcher::RemoteDataFetcherCallback& callback,
    std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> image_pull_secret_provider,
    const std::string& registry)
    : RemoteDataFetcher(cm, uri, "", callback),
      image_pull_secret_provider_(std::move(image_pull_secret_provider)), registry_(registry) {}

void ImageManifestFetcher::fetch() {
  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(uri_);
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);

  if (image_pull_secret_provider_) {
    const auto& image_pull_secret_raw = image_pull_secret_provider_->secret();
    if (image_pull_secret_raw.empty()) {
      ENVOY_LOG(debug, "fetch oci image from [uri = {}]: image pull secret empty", uri_.uri());
    } else {
      auto basic_authz_header = Oci::prepareAuthorizationHeader(image_pull_secret_raw, registry_);
      if (basic_authz_header.ok()) {
        message->headers().setCopy(Http::CustomHeaders::get().Authorization,
                                   basic_authz_header.value());
      } else {
        ENVOY_LOG(error,
                  "fetch oci image from [uri = {}]: failed to prepare Authorization header: ",
                  basic_authz_header.status().message());
        // TODO(jewertow): add failure reason "Internal"
        callback_.onFailure(Config::DataFetcher::FailureReason::Network);
      }
    }
  } else {
    ENVOY_LOG(debug, "fetch oci image from [uri = {}]: image pull secret provider is null",
              uri_.uri());
  }

  ENVOY_LOG(debug, "fetch oci image from [uri = {}]: start", uri_.uri());
  const auto thread_local_cluster = cm_.getThreadLocalCluster(uri_.cluster());
  if (thread_local_cluster != nullptr) {
    request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(message), *this,
        Http::AsyncClient::RequestOptions().setTimeout(
            std::chrono::milliseconds(DurationUtil::durationToMilliseconds(uri_.timeout()))));
  } else {
    ENVOY_LOG(error, "fetch oci image [uri = {}]: no cluster {}", uri_.uri(), uri_.cluster());
    callback_.onFailure(Config::DataFetcher::FailureReason::Network);
  }
}

void ImageManifestFetcher::onSuccess(const Http::AsyncClient::Request&,
                                     Http::ResponseMessagePtr&& response) {
  const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code != enumToInt(Http::Code::OK)) {
    onInvalidData(fmt::format("failed to fetch oci image [uri = {}, status code {}, body = {}]",
                              uri_.uri(), response->body().toString(), status_code));
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

void ImageManifestFetcher::onInvalidData(std::string error_message) {
  ENVOY_LOG(error, error_message);
  callback_.onFailure(Config::DataFetcher::FailureReason::InvalidData);
  request_ = nullptr;
}

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
