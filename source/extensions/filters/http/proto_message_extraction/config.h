#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/proto_message_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_extraction/v3/config.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/proto_message_extraction/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {

class FilterFactoryCreator
    : public Envoy::Extensions::HttpFilters::Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::proto_message_extraction::v3::
              ProtoMessageExtractionConfig> {
public:
  FilterFactoryCreator();

private:
  absl::StatusOr<Envoy::Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_message_extraction::v3::
          ProtoMessageExtractionConfig& proto_config,
      const std::string&, Envoy::Server::Configuration::FactoryContext&) override;

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::proto_message_extraction::v3::
          ProtoMessageExtractionConfig& proto_config,
      const std::string&, Envoy::Server::Configuration::ServerFactoryContext&) override;
};
} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
