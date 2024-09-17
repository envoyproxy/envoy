#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/header_mutation/v3/header_mutation.pb.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_mutation.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

using ProtoConfig = envoy::extensions::filters::http::header_mutation::v3::HeaderMutation;
using PerRouteProtoConfig =
    envoy::extensions::filters::http::header_mutation::v3::HeaderMutationPerRoute;
using MutationsProto = envoy::extensions::filters::http::header_mutation::v3::Mutations;

class Mutations {
public:
  using HeaderMutations = Http::HeaderMutations;

  Mutations(const MutationsProto& config)
      : request_mutations_(THROW_OR_RETURN_VALUE(
            HeaderMutations::create(config.request_mutations()), std::unique_ptr<HeaderMutations>)),
        response_mutations_(
            THROW_OR_RETURN_VALUE(HeaderMutations::create(config.response_mutations()),
                                  std::unique_ptr<HeaderMutations>)) {}

  void mutateRequestHeaders(Http::HeaderMap& headers, const Formatter::HttpFormatterContext& ctx,
                            const StreamInfo::StreamInfo& stream_info) const;
  void mutateResponseHeaders(Http::HeaderMap& headers, const Formatter::HttpFormatterContext& ctx,
                             const StreamInfo::StreamInfo& stream_info) const;

private:
  const std::unique_ptr<HeaderMutations> request_mutations_;
  const std::unique_ptr<HeaderMutations> response_mutations_;
};

class PerRouteHeaderMutation : public Router::RouteSpecificFilterConfig {
public:
  PerRouteHeaderMutation(const PerRouteProtoConfig& config);

  const Mutations& mutations() const { return mutations_; }

private:
  Mutations mutations_;
};
using PerRouteHeaderMutationSharedPtr = std::shared_ptr<PerRouteHeaderMutation>;

class HeaderMutationConfig {
public:
  HeaderMutationConfig(const ProtoConfig& config);

  const Mutations& mutations() const { return mutations_; }

  bool mostSpecificHeaderMutationsWins() const { return most_specific_header_mutations_wins_; }

private:
  Mutations mutations_;
  const bool most_specific_header_mutations_wins_;
};
using HeaderMutationConfigSharedPtr = std::shared_ptr<HeaderMutationConfig>;

class HeaderMutation : public Http::PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  HeaderMutation(HeaderMutationConfigSharedPtr config) : config_(std::move(config)) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;

private:
  HeaderMutationConfigSharedPtr config_{};
  // The lifetime of route config pointers is same as the matched route.
  absl::InlinedVector<std::reference_wrapper<const PerRouteHeaderMutation>, 4> route_configs_{};
};

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
