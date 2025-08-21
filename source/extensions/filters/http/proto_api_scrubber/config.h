#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

class FilterFactoryCreator
    : public Envoy::Extensions::HttpFilters::Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig> {
public:
  FilterFactoryCreator();

private:
  absl::StatusOr<Envoy::Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
          proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;
};
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
