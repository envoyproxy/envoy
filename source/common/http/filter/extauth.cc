#include "common/http/filter/extauth.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {

static LowerCaseString header_to_add(std::string("x-ambassador-calltype"));
static LowerCaseString value_to_add(std::string("extauth-request"));

ExtAuth::ExtAuth(ExtAuthConfigConstSharedPtr config) : config_(config) {}

ExtAuth::~ExtAuth() { ASSERT(!auth_request_); }

void ExtAuth::dumpHeaders(const char *what, HeaderMap* headers) {
  log().info("ExtAuth headers ({}):", what);

  headers->iterate(
    [](const HeaderEntry& header, void* context) -> void {
      (void)context;
      log().trace("  '{}':'{}'",
                  header.key().c_str(), header.value().c_str());
    },
    nullptr);
}

FilterHeadersStatus ExtAuth::decodeHeaders(HeaderMap& headers, bool) {

  // Request external authentication
  auth_complete_ = false;
  request_headers_ = &headers;
  dumpHeaders("decodeHeaders", request_headers_);

  MessagePtr reqmsg(new RequestMessageImpl(HeaderMapPtr{new HeaderMapImpl(headers)}));

  if (!config_->path_prefix_.empty()) {
    std::string path = reqmsg->headers().insertPath().value().c_str();

    path = config_->path_prefix_ + path;

    reqmsg->headers().insertPath().value(path);
  }

  // reqmsg->headers().insertMethod().value(Http::Headers::get().MethodValues.Post);
  // reqmsg->headers().insertPath().value(std::string("/ambassador/auth"));
  reqmsg->headers().insertHost().value(config_->cluster_); // cluster name is Host: header value!
  reqmsg->headers().insertContentLength().value(uint64_t(0));

  reqmsg->headers().addReference(header_to_add, value_to_add.get());

  log().info("ExtAuth contacting auth server");
  // reqmsg->body() = Buffer::InstancePtr(new Buffer::OwnedImpl(request_body));
  auth_request_ =
      config_->cm_.httpAsyncClientForCluster(config_->cluster_)
          .send(std::move(reqmsg), *this, Optional<std::chrono::milliseconds>(config_->timeout_));
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

  dumpHeaders("onSuccess", request_headers_);

  uint64_t response_code = Http::Utility::getResponseStatus(response->headers());
  std::string response_body(response->bodyAsString());

  log().info("ExtAuth Auth responded with code {}", response_code);

  if (!response_body.empty()) {
    log().info("ExtAuth Auth said: {}", response->bodyAsString());
  }

  if (response_code != enumToInt(Http::Code::OK)) {
    log().info("ExtAuth rejecting request");

    config_->stats_.rq_rejected_.inc();
    request_headers_ = nullptr;

    Http::HeaderMapPtr response_headers{new HeaderMapImpl(response->headers())};

    callbacks_->encodeHeaders(std::move(response_headers), response_body.empty());

    if (!response_body.empty()) {
      Buffer::OwnedImpl buffer(response_body);
      callbacks_->encodeData(buffer, true);
    }

    return;
  }

  log().info("ExtAuth accepting request");
  bool addedHeaders = false;

  // Do we have any headers configured to copy?

  if (!config_->allowed_headers_.empty()) {
    // Yup. Let's see if any of them are present. 

    for (std::string allowed_header : config_->allowed_headers_) {
      LowerCaseString key(allowed_header);

      // OK, do we have this header?

      const HeaderEntry* hdr = response->headers().get(key);

      if (hdr) {
        // Well, the header exists at all. Does it have a value?

        const HeaderString& value = hdr->value();

        if (!value.empty()) {
          // Not empty! Copy it into our request_headers_.

          std::string valstr(value.c_str());

          ENVOY_STREAM_LOG(trace, "ExtAuth allowing response header {}: {}", *callbacks_, allowed_header, valstr);
          addedHeaders = true;
          request_headers_->addCopy(key, valstr);
        }
      }
    }
  }

  if (addedHeaders) {
    // Yup, we added headers. Invalidate the route cache in case any of 
    // the headers will affect routing decisions.

    dumpHeaders("ExtAuth invalidating route cache", request_headers_);

    callbacks_->clearRouteCache();
  }

  config_->stats_.rq_passed_.inc();
  auth_complete_ = true;
  request_headers_ = nullptr;
  callbacks_->continueDecoding();
}

void ExtAuth::onFailure(Http::AsyncClient::FailureReason) {
  auth_request_ = nullptr;
  request_headers_ = nullptr;
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
