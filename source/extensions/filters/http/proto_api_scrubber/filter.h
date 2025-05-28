#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

inline constexpr const char kFilterName[] = "envoy.filters.http.proto_api_scrubber";

class Filter : public Envoy::Http::PassThroughFilter,
               Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  explicit Filter(FilterConfig&);

  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                 bool end_stream) override;

  Envoy::Http::FilterDataStatus decodeData(Envoy::Buffer::Instance& data, bool end_stream) override;

  Envoy::Http::FilterHeadersStatus encodeHeaders(Envoy::Http::ResponseHeaderMap& headers,
                                                 bool end_stream) override;

  Envoy::Http::FilterDataStatus encodeData(Envoy::Buffer::Instance& data, bool end_stream) override;
};

class FilterFactory
    : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig> {
private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
          proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;
};
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
