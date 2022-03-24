#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using ::Envoy::Router::RouteConstSharedPtr;
using Http::FilterHeadersStatus;

Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  Envoy::Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    // Nothing to do if no route, continue the filter chain iteration.
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  state_ = State::Calling;
  initiating_call_ = true;

  Envoy::Upstream::ThreadLocalCluster* cluster =
      context_.clusterManager().getThreadLocalCluster(route->routeEntry()->clusterName());

  std::string audience_str;
  if (cluster != nullptr) {
    auto filter_metadata = cluster->info()->metadata().filter_metadata();
    const auto filter_it = filter_metadata.find(std::string(FilterName));

    if (filter_it != filter_metadata.end()) {
      audience_str =
          filter_it->second.fields().find(std::string(AudienceKey))->second.string_value();
    }
  }

  if (!audience_str.empty()) {
    // Add the audience from the config to the final url.
    // `[AUDIENCE]` field is substituted with real audience string from the config.
    std::string final_url =
        absl::StrReplaceAll(filter_config_->http_uri().uri(), {{"[AUDIENCE]", audience_str}});
    client_->fetchToken(*this, buildRequest("GET", final_url));
    initiating_call_ = false;
  } else {
    // Audience is URL of receiving service that will performance authentication. So, there is no
    // need to fetch the token if no audience is specified, because no authentication will be
    // performed. Just continue the filter chain iteration.
    ENVOY_LOG(error, "Failed to retrieve the audience from the configuration.");
    state_ = State::Complete;
  }

  return state_ == State::Complete ? FilterHeadersStatus::Continue
                                   : Http::FilterHeadersStatus::StopIteration;
}

void GcpAuthnFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GcpAuthnFilter::onComplete(const Http::ResponseMessage*) {
  state_ = State::Complete;
  if (!initiating_call_) {
    decoder_callbacks_->continueDecoding();
  }
  // TODO(tyxia) Decode jwt token from the response (e.g., get the exp time for cache.)
  // Integration test already hit this path, so add test coverage once decode path is added.
}

void GcpAuthnFilter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
