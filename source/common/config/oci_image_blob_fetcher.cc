#include "source/common/config/oci_image_manifest_fetcher.h"

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"

namespace Envoy {
namespace Config {
namespace DataFetcher {

OciImageBlobFetcher::OciImageBlobFetcher(Upstream::ClusterManager& cm,
                                         const envoy::config::core::v3::HttpUri& uri,
                                         const std::string& authz_header_value,
                                         const std::string& digest,
                                         const std::string& content_hash,
                                         RemoteDataFetcherCallback& callback)
    : cm_(cm), uri_(uri), authz_header_value_(authz_header_value), digest_(digest), content_hash_(content_hash), callback_(callback) {}

OciImageBlobFetcher::~OciImageBlobFetcher() { cancel(); }

void OciImageBlobFetcher::cancel() {
  if (request_) {
    request_->cancel();
    ENVOY_LOG(debug, "fetch oci image blob [uri = {}]: canceled", uri_.uri());
  }

  request_ = nullptr;
}

void OciImageBlobFetcher::fetch() {
  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(uri_);
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
  message->headers().setAuthorization(authz_header_value_);
  // TODO: add "accept: application/vnd.oci.image.manifest.v1+json"
  ENVOY_LOG(info, "fetch oci image blob from [uri = {}]: start", uri_.uri());
  const auto thread_local_cluster = cm_.getThreadLocalCluster(uri_.cluster());
  if (thread_local_cluster != nullptr) {
    request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(message), *this,
        Http::AsyncClient::RequestOptions().setTimeout(
            std::chrono::milliseconds(DurationUtil::durationToMilliseconds(uri_.timeout()))));
  } else {
    ENVOY_LOG(info, "fetch oci image blob [uri = {}]: no cluster {}", uri_.uri(), uri_.cluster());
    callback_.onFailure(FailureReason::Network);
  }
}

void OciImageBlobFetcher::onSuccess(const Http::AsyncClient::Request&,
                                  Http::ResponseMessagePtr&& response) {
  const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code == enumToInt(Http::Code::OK)) {
    ENVOY_LOG(info, "fetch oci image blob [uri = {}, body = {}]: success", uri_.uri(), response->body().toString());
    if (response->body().length() > 0) {
      auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
      const auto content_hash = Hex::encode(crypto_util.getSha256Digest(response->body()));

      // TODO(jewertow)
      // if (content_hash_ != content_hash) {
      //   ENVOY_LOG(info, "fetch oci image [uri = {}]: data is invalid", uri_.uri());
      //   callback_.onFailure(FailureReason::InvalidData);
      // } else {

      callback_.onSuccess(response->bodyAsString());
    } else {
      ENVOY_LOG(info, "fetch oci image blob [uri = {}]: body is empty", uri_.uri());
      callback_.onFailure(FailureReason::Network);
    }
  } else if (status_code == enumToInt(Http::Code::TemporaryRedirect)) {
    auto location = response->headers().get(Http::Headers::get().Location);
    if (location.empty()) {
      ENVOY_LOG(error, "fetch oci image blob [uri = {}, status code = {}]: location empty", uri_.uri(), status_code);
      callback_.onFailure(FailureReason::Network);
    } else {
      auto location_value = location[0]->value().getStringView();
      ENVOY_LOG(error, "fetch oci image blob [uri = {}, status code = {}]: redirected to {}", uri_.uri(), status_code, location_value);

      envoy::config::core::v3::HttpUri uri;
      uri.set_cluster("oci_registry_blob_redirect_location");
      uri.set_uri(location_value);
      Http::RequestMessagePtr message = Http::Utility::prepareHeaders(uri);
      message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
      // TODO(jewertow): set Authorization header if the token is not included in the URL path, e.g. docker hub.
      ENVOY_LOG(info, "fetch oci image blob from temporary location [uri = {}]: start", uri.uri());

      const auto thread_local_cluster = cm_.getThreadLocalCluster(uri.cluster());
      if (thread_local_cluster != nullptr) {
        request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(message), *this,
        Http::AsyncClient::RequestOptions().setTimeout(
            std::chrono::milliseconds(DurationUtil::durationToMilliseconds(uri.timeout()))));
      } else {
        ENVOY_LOG(info, "fetch oci image blob [uri = {}]: no cluster {}", uri_.uri(), uri_.cluster());
        callback_.onFailure(FailureReason::Network);
      }
    }
  } else {
    ENVOY_LOG(info, "fetch oci image blob [uri = {}, body = {}]: response status code {}", uri_.uri(), response->body().toString(),
              status_code);
    callback_.onFailure(FailureReason::Network);
  }

  request_ = nullptr;
}

void OciImageBlobFetcher::onFailure(const Http::AsyncClient::Request&,
                                  Http::AsyncClient::FailureReason reason) {
  ENVOY_LOG(info, "fetch oci image blob [uri = {}]: network error {}", uri_.uri(), enumToInt(reason));
  request_ = nullptr;
  callback_.onFailure(FailureReason::Network);
}

} // namespace DataFetcher
} // namespace Config
} // namespace Envoy
