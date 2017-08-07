#include "common/http/filter/extauth.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {

static LowerCaseString header_to_add(std::string("x-ark3-stuff"));

ExtAuth::ExtAuth(ExtAuthConfigConstSharedPtr config) : config_(config) {}

ExtAuth::~ExtAuth() { ASSERT(!auth_request_); }

FilterHeadersStatus ExtAuth::decodeHeaders(HeaderMap& headers, bool) {
  log().info("ExtAuth Request received; contacting auth server");

  // Copy original headers as a JSON object
  std::string json("{");
  headers.iterate(
      [](const HeaderEntry& header, void* ctx) -> void {
        std::string* jsonPtr = static_cast<std::string*>(ctx);
        std::string key(header.key().c_str());
        std::string value(header.value().c_str());
        // TODO(ark3): Ensure that key and value are sane so generated JSON is valid
        *jsonPtr += "\n \"" + key + "\": \"" + value + "\",";
      },
      &json);
  std::string request_body = json.substr(0, json.size() - 1) + "\n}"; // Drop trailing comma

  // Request external authentication
  auth_complete_ = false;
  MessagePtr request(new RequestMessageImpl());
  request->headers().insertMethod().value(Http::Headers::get().MethodValues.Post);
  request->headers().insertPath().value(std::string("/ambassador/auth"));
  request->headers().insertHost().value(config_->cluster_); // cluster name is Host: header value!
  request->headers().insertContentType().value(std::string("application/json"));
  request->headers().insertContentLength().value(request_body.size());
  request->body() = Buffer::InstancePtr(new Buffer::OwnedImpl(request_body));
  auth_request_ =
      config_->cm_.httpAsyncClientForCluster(config_->cluster_)
          .send(std::move(request), *this, Optional<std::chrono::milliseconds>(config_->timeout_));
  // .send(...) -> onSuccess(...) or onFailure(...)
  // This handle can be used to ->cancel() the request.

  // Stop until we have a result
  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus ExtAuth::decodeData(Buffer::Instance&, bool) {
  if (auth_complete_) {
    return FilterDataStatus::Continue;
  }
  return FilterDataStatus::StopIterationAndBuffer;
}

FilterTrailersStatus ExtAuth::decodeTrailers(HeaderMap&) {
  if (auth_complete_) {
    return FilterTrailersStatus::Continue;
  }
  return FilterTrailersStatus::StopIteration;
}

ExtAuthStats ExtAuth::generateStats(const std::string& prefix, Stats::Store& store) {
  std::string final_prefix = prefix + "extauth.";
  return {ALL_EXTAUTH_STATS(POOL_COUNTER_PREFIX(store, final_prefix))};
}

void ExtAuth::onSuccess(Http::MessagePtr&& response) {
  auth_request_ = nullptr;
  uint64_t response_code = Http::Utility::getResponseStatus(response->headers());
  std::string response_body(response->bodyAsString());
  log().info("ExtAuth Auth responded with code {}", response_code);
  if (!response_body.empty()) {
    log().info("ExtAuth Auth said: {}", response->bodyAsString());
  }

  if (response_code != enumToInt(Http::Code::OK)) {
    log().info("ExtAuth rejecting request");
    config_->stats_.rq_rejected_.inc();
    Http::HeaderMapPtr response_headers{new HeaderMapImpl(response->headers())};
    callbacks_->encodeHeaders(std::move(response_headers), response_body.empty());
    if (!response_body.empty()) {
      Buffer::OwnedImpl buffer(response_body);
      callbacks_->encodeData(buffer, true);
    }
    return;
  }

  log().info("ExtAuth accepting request");
  config_->stats_.rq_passed_.inc();
  auth_complete_ = true;
  callbacks_->continueDecoding();
}

void ExtAuth::onFailure(Http::AsyncClient::FailureReason) {
  auth_request_ = nullptr;
  log().warn("ExtAuth Auth request failed");
  config_->stats_.rq_failed_.inc();
  Http::Utility::sendLocalReply(*callbacks_, false, Http::Code::ServiceUnavailable,
                                std::string("Auth request failed."));
}

void ExtAuth::onDestroy() {
  if (auth_request_) {
    auth_request_->cancel();
    auth_request_ = nullptr;
  }
}

void ExtAuth::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // Http
} // Envoy
