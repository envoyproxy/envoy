#include "source/extensions/filters/http/credentials/filter.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/common/matchers.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

namespace {
  constexpr absl::string_view UnauthorizedBodyMessage = "Credential aquisition flow failed.";
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::credentials::v3alpha::Injector&,
    CredentialSourcePtr credential_source,
    Stats::Scope& scope, const std::string& stats_prefix)
    : stats_(FilterConfig::generateStats(stats_prefix, scope)), credential_source_(std::move(credential_source)) {
}

FilterStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
    return {ALL_CREDENTIAL_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

Filter::Filter(FilterConfigSharedPtr config) :
  config_(std::move(config)) {
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const auto authorization = headers.get(Http::CustomHeaders::get().Authorization);
  if (!authorization.empty()) {
      // skip requests with an explicit `Authorization` header
      return Http::FilterHeadersStatus::Continue;
  }

  request_headers_ = &headers;

  in_flight_credential_request_ = config_->credentialSource().requestCredential(*this);

  // pause while we await the next step from the OAuth server
  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

void Filter::onSuccess(std::string credential) {
  decoder_callbacks_->dispatcher().post([this, credential]() {
    request_headers_->setCopy(Http::CustomHeaders::get().Authorization, credential);

    config_->stats().credential_injected_.inc();

    decoder_callbacks_->continueDecoding();
  });
}

void Filter::onFailure() {
  config_->stats().credential_error_.inc();
  decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, UnauthorizedBodyMessage, nullptr,
                                     absl::nullopt, EMPTY_STRING);
}

void Filter::onDestroy() {
  if (in_flight_credential_request_ != nullptr) {
      in_flight_credential_request_->cancel();
  }
}

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
