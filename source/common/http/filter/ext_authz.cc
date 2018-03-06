#include "common/http/filter/ext_authz.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/ext_authz/ext_authz_impl.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace Http {
namespace ExtAuthz {

namespace {

const Http::HeaderMap* getDeniedHeader() {
  static const Http::HeaderMap* header_map = new Http::HeaderMapImpl{
      {Http::Headers::get().Status, std::to_string(enumToInt(Code::Forbidden))}};
  return header_map;
}

} // namespace

void Filter::initiateCall(const HeaderMap& headers) {
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

  Envoy::ExtAuthz::CheckRequestUtils::createHttpCheck(callbacks_, headers, check_request_);

  state_ = State::Calling;
  initiating_call_ = true;
  client_->check(*this, check_request_, callbacks_->activeSpan());
  initiating_call_ = false;
}

FilterHeadersStatus Filter::decodeHeaders(HeaderMap& headers, bool) {
  initiateCall(headers);
  return state_ == State::Calling ? FilterHeadersStatus::StopIteration
                                  : FilterHeadersStatus::Continue;
}

FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  return state_ == State::Calling ? FilterDataStatus::StopIterationAndWatermark
                                  : FilterDataStatus::Continue;
}

FilterTrailersStatus Filter::decodeTrailers(HeaderMap&) {
  return state_ == State::Calling ? FilterTrailersStatus::StopIteration
                                  : FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void Filter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

void Filter::onComplete(Envoy::ExtAuthz::CheckStatus status) {
  ASSERT(cluster_);

  state_ = State::Complete;

  using Envoy::ExtAuthz::CheckStatus;

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
                                             enumToInt(Code::Forbidden),
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
    Http::HeaderMapPtr response_headers{new HeaderMapImpl(*getDeniedHeader())};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    callbacks_->requestInfo().setResponseFlag(Envoy::RequestInfo::ResponseFlag::Unauthorized);
  } else {
    // We can get completion inline, so only call continue if that isn't happening.
    if (!initiating_call_) {
      callbacks_->continueDecoding();
    }
  }
}

} // namespace ExtAuthz
} // namespace Http
} // namespace Envoy
