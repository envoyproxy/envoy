#include "extensions/filters/http/ext_authz/ext_authz.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

#include "extensions/filters/common/ext_authz/ext_authz_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

namespace {

const Http::HeaderMap* getDeniedHeader() {
  static const Http::HeaderMap* header_map = new Http::HeaderMapImpl{
      {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Forbidden))}};
  return header_map;
}

} // namespace

void Filter::initiateCall(const Http::HeaderMap& headers) {
  Router::RouteConstSharedPtr route = callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    return;
  }

  const Router::RouteEntry* route_entry = route->routeEntry();
  Upstream::ThreadLocalCluster* cluster = config_->cm().get(route_entry->clusterName());
  if (cluster == nullptr) {
    return;
  }
  cluster_ = cluster->info();

  Filters::Common::ExtAuthz::CheckRequestUtils::createHttpCheck(callbacks_, headers,
                                                                check_request_);

  state_ = State::Calling;
  initiating_call_ = true;
  client_->check(*this, check_request_, callbacks_->activeSpan());
  initiating_call_ = false;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool) {
  initiateCall(headers);
  return state_ == State::Calling ? Http::FilterHeadersStatus::StopIteration
                                  : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  return state_ == State::Calling ? Http::FilterDataStatus::StopIterationAndWatermark
                                  : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap&) {
  return state_ == State::Calling ? Http::FilterTrailersStatus::StopIteration
                                  : Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void Filter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

void Filter::onComplete(Filters::Common::ExtAuthz::CheckStatus status) {
  ASSERT(cluster_);

  state_ = State::Complete;

  using Filters::Common::ExtAuthz::CheckStatus;

  switch (status) {
  case CheckStatus::OK:
    cluster_->statsScope().counter("ext_authz.ok").inc();
    break;
  case CheckStatus::Error:
    cluster_->statsScope().counter("ext_authz.error").inc();
    break;
  case CheckStatus::Denied:
    cluster_->statsScope().counter("ext_authz.denied").inc();
    Http::CodeUtility::ResponseStatInfo info{config_->scope(),
                                             cluster_->statsScope(),
                                             EMPTY_STRING,
                                             enumToInt(Http::Code::Forbidden),
                                             true,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             false};
    Http::CodeUtility::chargeResponseStat(info);
    break;
  }

  // We fail open/fail close based of filter config
  // if there is an error contacting the service.
  if (status == CheckStatus::Denied ||
      (status == CheckStatus::Error && !config_->failureModeAllow())) {
    Http::HeaderMapPtr response_headers{new Http::HeaderMapImpl(*getDeniedHeader())};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    callbacks_->requestInfo().setResponseFlag(
        RequestInfo::ResponseFlag::UnauthorizedExternalService);
  } else {
    // We can get completion inline, so only call continue if that isn't happening.
    if (!initiating_call_) {
      callbacks_->continueDecoding();
    }
  }
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
