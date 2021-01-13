#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

using FactoryMap =
    absl::flat_hash_map<std::string,
                        Http::FilterFactoryCb>;

class Filter : public Http::PassThroughFilter {
public:
  explicit Filter(FactoryMap factories) : factories_(std::move(factories)) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&,
                                          bool) override {
    decoded_headers_ = true;
    // TODO(snowp): Support buffering the request until a match result has been determined instead
    // of assuming it will only be header matching.
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    encoded_headers_ = true;

    // TODO(snowp): Support buffering the response until a match result has been determined instead
    // of assuming it will only be header matching.
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&,
                                              bool) override {
    encoded_headers_ = true;

    // TODO(snowp): Support buffering the response until a match result has been determined instead
    // of assuming it will only be header matching.
    return Http::FilterHeadersStatus::Continue;
  }

  void onMatchCallback(const Matcher::Action&) override {
  }

private:
  const FactoryMap factories_;
  bool decoded_headers_ : 1;
  bool encoded_headers_ : 1;
};

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy