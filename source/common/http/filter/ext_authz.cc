#include "common/http/filter/ext_authz.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"
#include "common/ext_authz/ext_authz_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace Http {
namespace ExtAuthz {

namespace {

static const Http::HeaderMap* getDeniedHeader() {
  static const Http::HeaderMap* header_map = new Http::HeaderMapImpl{
      {Http::Headers::get().Status, std::to_string(enumToInt(Code::Forbidden))}};
  return header_map;
}

} // namespace

void Filter::setCheckReqGen(Envoy::ExtAuthz::CheckRequestGenIntf *crg)
{
  ASSERT(crg_ == nullptr);
  crg_ = Envoy::ExtAuthz::CheckRequestGenIntfPtr{std::move(crg)};
}

void Filter::initiateCall(const HeaderMap& headers) {

  Router::RouteConstSharedPtr route = callbacks_->route();
  if (!route || !route->routeEntry()) {
    return;
  }

  const Router::RouteEntry* route_entry = route->routeEntry();
  Upstream::ThreadLocalCluster* cluster = config_->cm().get(route_entry->clusterName());
  if (!cluster) {
    return;
  }
  cluster_ = cluster->info();

  if (crg_ == nullptr) {
    setCheckReqGen(new Envoy::ExtAuthz::CheckRequestGen());
  }
  envoy::api::v2::auth::CheckRequest request;
  crg_->createHttpCheck(callbacks_, headers, request);

  state_ = State::Calling;
  initiating_call_ = true;
  client_->check(*this, request, callbacks_->activeSpan());
  initiating_call_ = false;
}

FilterHeadersStatus Filter::decodeHeaders(HeaderMap& headers, bool) {
  initiateCall(headers);
  return (state_ == State::Calling || state_ == State::Responded)
             ? FilterHeadersStatus::StopIteration
             : FilterHeadersStatus::Continue;
}

FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  ASSERT(state_ != State::Responded);
  if (state_ != State::Calling) {
    return FilterDataStatus::Continue;
  }
  // If the request is too large, stop reading new data until the buffer drains.
  return FilterDataStatus::StopIterationAndWatermark;
}

FilterTrailersStatus Filter::decodeTrailers(HeaderMap&) {
  ASSERT(state_ != State::Responded);
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

void Filter::complete(Envoy::ExtAuthz::CheckStatus status) {
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
    cluster_->statsScope().counter("ext_authz.unauthz").inc();
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
      (status == CheckStatus::Error && !config_->failOpen())) {
    state_ = State::Responded;
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
