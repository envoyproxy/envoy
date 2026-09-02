#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/matching/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/matching/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace DynamicModules {

// Type aliases for the function pointers resolved from the module.
using OnDataInputConfigNewType = decltype(&envoy_dynamic_module_on_matcher_data_input_config_new);
using OnDataInputConfigDestroyType =
    decltype(&envoy_dynamic_module_on_matcher_data_input_config_destroy);
using OnDataInputGetType = decltype(&envoy_dynamic_module_on_matcher_data_input_get);

// Shared ownership of the dynamic module so it stays loaded while any data input references it.
using DataInputModuleSharedPtr = std::shared_ptr<Extensions::DynamicModules::DynamicModule>;

// Evaluation state passed to the module during a single get. The data input pointer handed to the
// module points at this struct, and the module reads headers and writes the result through the
// matcher data input callbacks. Valid only during the get event hook.
struct DataInputContext {
  const ::Envoy::Http::RequestHeaderMap* request_headers{};
  const ::Envoy::Http::ResponseHeaderMap* response_headers{};
  const ::Envoy::Http::ResponseTrailerMap* response_trailers{};
  std::string result;
  bool has_result{false};
};

// Data input that delegates value extraction to a dynamic module. The module returns a string that
// downstream map matchers dispatch on, so a module can select one of many matches with a single
// evaluation and without clearing the route cache.
class DynamicModuleDataInput : public ::Envoy::Matcher::DataInput<::Envoy::Http::HttpMatchingData> {
public:
  DynamicModuleDataInput(DataInputModuleSharedPtr module, OnDataInputGetType on_get,
                         std::shared_ptr<const void> in_module_config);

  // The produced value is a string, so the default "string" data input type is inherited, which
  // lets exact, prefix, domain, and IP range map matchers dispatch on it.
  ::Envoy::Matcher::DataInputGetResult
  get(const ::Envoy::Http::HttpMatchingData& data) const override;

private:
  // Prevent copy/move.
  DynamicModuleDataInput(const DynamicModuleDataInput&) = delete;
  DynamicModuleDataInput& operator=(const DynamicModuleDataInput&) = delete;

  DataInputModuleSharedPtr module_;
  OnDataInputGetType on_get_;
  // Shared owner of the in-module configuration. The configuration is destroyed exactly once, after
  // the last data input instance and the factory callback that built it are released.
  std::shared_ptr<const void> in_module_config_;
};

class DynamicModuleDataInputFactory
    : public ::Envoy::Matcher::DataInputFactory<::Envoy::Http::HttpMatchingData> {
public:
  // The registered name keeps "string" because the sibling data input already registers
  // "envoy.matching.inputs.dynamic_module_data_input".
  std::string name() const override {
    return "envoy.matching.inputs.dynamic_module_string_data_input";
  }

  ::Envoy::Matcher::DataInputFactoryCb<::Envoy::Http::HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::http::dynamic_modules::v3::DynamicModuleDataInput>();
  }
};

DECLARE_FACTORY(DynamicModuleDataInputFactory);

} // namespace DynamicModules
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
