#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/header_mutation/v3/header_mutation.pb.h"
#include "envoy/http/query_params.h"

#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_mutation.h"
#include "source/common/http/utility.h"
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
using ParameterMutationProto = envoy::config::core::v3::KeyValueMutation;
using ParameterAppendProto = envoy::config::core::v3::KeyValueAppend;

/**
 * Interface for mutating query parameters.
 */
class QueryParameterMutation {
public:
  virtual ~QueryParameterMutation() = default;

  /**
   * Mutate the query parameters.
   * @param params the query parameters to mutate.
   * @param context the formatter context.
   * @param stream_info the stream info.
   */
  virtual void mutateQueryParameter(Http::Utility::QueryParamsMulti& params,
                                    const Formatter::HttpFormatterContext& context,
                                    const StreamInfo::StreamInfo& stream_info) const PURE;
};
using QueryParameterMutationPtr = std::unique_ptr<QueryParameterMutation>;

class QueryParameterMutationRemove : public QueryParameterMutation {
public:
  QueryParameterMutationRemove(absl::string_view key) : key_(key) {}

  // QueryParameterMutation
  void mutateQueryParameter(Http::Utility::QueryParamsMulti& params,
                            const Formatter::HttpFormatterContext&,
                            const StreamInfo::StreamInfo&) const override {
    params.remove(key_);
  }

private:
  const std::string key_;
};

class QueryParameterMutationAppend : public QueryParameterMutation {
public:
  QueryParameterMutationAppend(absl::string_view key, Formatter::FormatterPtr formatter,
                               ParameterAppendProto::KeyValueAppendAction action)
      : key_(key), formatter_(std::move(formatter)), action_(action) {}

  // QueryParameterMutation
  void mutateQueryParameter(Http::Utility::QueryParamsMulti& params,
                            const Formatter::HttpFormatterContext& context,
                            const StreamInfo::StreamInfo& stream_info) const override;

private:
  const std::string key_;
  const Formatter::FormatterPtr formatter_;
  const ParameterAppendProto::KeyValueAppendAction action_{};
};

class Mutations {
public:
  using HeaderMutations = Http::HeaderMutations;

  Mutations(const MutationsProto& config, Server::Configuration::ServerFactoryContext& context,
            absl::Status& creation_status);

  void mutateRequestHeaders(Http::RequestHeaderMap& headers,
                            const Formatter::HttpFormatterContext& context,
                            const StreamInfo::StreamInfo& stream_info) const;
  void mutateResponseHeaders(Http::ResponseHeaderMap& headers,
                             const Formatter::HttpFormatterContext& context,
                             const StreamInfo::StreamInfo& stream_info) const;
  void mutateResponseTrailers(Http::ResponseTrailerMap& trailers,
                              const Formatter::HttpFormatterContext& context,
                              const StreamInfo::StreamInfo& stream_info) const;
  void mutateRequestTrailers(Http::RequestTrailerMap& trailers,
                             const Formatter::HttpFormatterContext& context,
                             const StreamInfo::StreamInfo& stream_info) const;

private:
  std::unique_ptr<HeaderMutations> request_mutations_;
  std::vector<QueryParameterMutationPtr> query_query_parameter_mutations_;
  std::unique_ptr<HeaderMutations> response_mutations_;

  std::unique_ptr<HeaderMutations> response_trailers_mutations_;
  std::unique_ptr<HeaderMutations> request_trailers_mutations_;
};

class PerRouteHeaderMutation : public Router::RouteSpecificFilterConfig {
public:
  PerRouteHeaderMutation(const PerRouteProtoConfig& config,
                         Server::Configuration::ServerFactoryContext& context,
                         absl::Status& creation_status);

  const Mutations& mutations() const { return mutations_; }

private:
  Mutations mutations_;
};
using PerRouteHeaderMutationSharedPtr = std::shared_ptr<PerRouteHeaderMutation>;

class HeaderMutationConfig {
public:
  HeaderMutationConfig(const ProtoConfig& config,
                       Server::Configuration::ServerFactoryContext& context,
                       absl::Status& creation_status);

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

  // Http::StreamEncoderFilter
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

  // Http::StreamEncoderFilter
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

private:
  void maybeInitializeRouteConfigs(Http::StreamFilterCallbacks* callbacks);

  HeaderMutationConfigSharedPtr config_;
  // The lifetime of route config pointers is same as the matched route.
  bool route_configs_initialized_{false};
  absl::InlinedVector<std::reference_wrapper<const PerRouteHeaderMutation>, 4> route_configs_;
};

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
