#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/proto_message_scrubbing/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_scrubbing/v3/config.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/proto_message_scrubbing/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageScrubbing {

class FilterFactoryCreator : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
                                 envoy::extensions::filters::http::proto_message_scrubbing::v3::
                                     ProtoMessageScrubbingConfig> {
public:
  FilterFactoryCreator();

private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_message_scrubbing::v3::
          ProtoMessageScrubbingConfig& proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;
};
} // namespace ProtoMessageScrubbing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
