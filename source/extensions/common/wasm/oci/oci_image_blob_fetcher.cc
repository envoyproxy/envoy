#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/common/wasm/oci/oci_image_manifest_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

ImageBlobFetcher::ImageBlobFetcher(Upstream::ClusterManager& cm,
                                   const envoy::config::core::v3::HttpUri& uri,
                                   const std::string& content_hash,
                                   Config::DataFetcher::RemoteDataFetcherCallback& callback,
                                   const std::string& credential)
    : RemoteDataFetcher(cm, uri, content_hash, callback), credential_(credential) {}

void ImageBlobFetcher::fetch() {
  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(uri_);
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
  if (!credential_.empty()) {
    message->headers().setCopy(Http::CustomHeaders::get().Authorization, credential_);
  }

  ENVOY_LOG(debug, "fetch oci image blob from [uri = {}]: start", uri_.uri());
  const auto thread_local_cluster = cm_.getThreadLocalCluster(uri_.cluster());
  if (thread_local_cluster != nullptr) {
    request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(message), *this,
        Http::AsyncClient::RequestOptions().setTimeout(
            std::chrono::milliseconds(DurationUtil::durationToMilliseconds(uri_.timeout()))));
  } else {
    ENVOY_LOG(debug, "fetch oci image blob [uri = {}]: no cluster {}", uri_.uri(), uri_.cluster());
    callback_.onFailure(Config::DataFetcher::FailureReason::Network);
  }
}

void ImageBlobFetcher::onSuccess(const Http::AsyncClient::Request&,
                                 Http::ResponseMessagePtr&& response) {
  const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code == enumToInt(Http::Code::OK)) {
    ENVOY_LOG(debug, "fetch oci image blob [uri = {}]: success", uri_.uri());
    if (response->body().length() > 0) {
      auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
      const auto content_hash = Hex::encode(crypto_util.getSha256Digest(response->body()));
      if (content_hash_ != content_hash) {
        ENVOY_LOG(error, "fetch oci image [uri = {}]: data is invalid", uri_.uri());
        callback_.onFailure(Config::DataFetcher::FailureReason::InvalidData);
      } else {
        callback_.onSuccess(response->bodyAsString());
      }
    } else {
      ENVOY_LOG(error, "fetch oci image blob [uri = {}]: body is empty", uri_.uri());
      callback_.onFailure(Config::DataFetcher::FailureReason::Network);
    }
  } else if (status_code == enumToInt(Http::Code::TemporaryRedirect)) {
    auto location = response->headers().get(Http::Headers::get().Location);
    if (location.empty()) {
      ENVOY_LOG(error, "fetch oci image blob [uri = {}, status code = {}]: location empty",
                uri_.uri(), status_code);
      callback_.onFailure(Config::DataFetcher::FailureReason::Network);
    } else {
      auto location_value = location[0]->value().getStringView();
      // TODO(jewertow): sanitize query params to avoid logging tokens
      ENVOY_LOG(debug, "fetch oci image blob [uri = {}, status code = {}]: redirected to {}",
                uri_.uri(), status_code, location_value);

      envoy::config::core::v3::HttpUri uri;
      uri.set_cluster(uri_.cluster());
      uri.set_uri(location_value);
      Http::RequestMessagePtr message = Http::Utility::prepareHeaders(uri);
      message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
      // TODO(jewertow): set Authorization header if a token is not included in the provided
      // location, e.g. docker hub.
      ENVOY_LOG(debug, "fetch oci image blob from temporary location [uri = {}]: start", uri.uri());

      const auto thread_local_cluster = cm_.getThreadLocalCluster(uri.cluster());
      if (thread_local_cluster != nullptr) {
        request_ = thread_local_cluster->httpAsyncClient().send(
            std::move(message), *this,
            Http::AsyncClient::RequestOptions().setTimeout(
                std::chrono::milliseconds(DurationUtil::durationToMilliseconds(uri.timeout()))));
      } else {
        ENVOY_LOG(error, "fetch oci image blob [uri = {}]: no cluster {}", uri_.uri(),
                  uri_.cluster());
        callback_.onFailure(Config::DataFetcher::FailureReason::Network);
      }
    }
  } else {
    ENVOY_LOG(error, "fetch oci image blob [uri = {}, body = {}]: response status code {}",
              uri_.uri(), response->body().toString(), status_code);
    callback_.onFailure(Config::DataFetcher::FailureReason::Network);
  }

  request_ = nullptr;
}

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
