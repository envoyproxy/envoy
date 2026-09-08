#include "source/extensions/matching/http/dynamic_modules/string_data_input.h"

#include <string>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace DynamicModules {

using DynamicModuleDataInputProto =
    envoy::extensions::matching::http::dynamic_modules::v3::DynamicModuleDataInput;

namespace {

using HeadersMapOptConstRef = OptRef<const ::Envoy::Http::HeaderMap>;

HeadersMapOptConstRef headerMapByType(const DataInputContext* context,
                                      envoy_dynamic_module_type_http_header_type header_type) {
  switch (header_type) {
  case envoy_dynamic_module_type_http_header_type_RequestHeader:
    return makeOptRefFromPtr<const ::Envoy::Http::HeaderMap>(context->request_headers);
  case envoy_dynamic_module_type_http_header_type_ResponseHeader:
    return makeOptRefFromPtr<const ::Envoy::Http::HeaderMap>(context->response_headers);
  case envoy_dynamic_module_type_http_header_type_ResponseTrailer:
    return makeOptRefFromPtr<const ::Envoy::Http::HeaderMap>(context->response_trailers);
  default:
    return {};
  }
}

} // namespace

DynamicModuleDataInput::DynamicModuleDataInput(DataInputModuleSharedPtr module,
                                               OnDataInputGetType on_get,
                                               std::shared_ptr<const void> in_module_config)
    : module_(std::move(module)), on_get_(on_get), in_module_config_(std::move(in_module_config)) {}

::Envoy::Matcher::DataInputGetResult
DynamicModuleDataInput::get(const ::Envoy::Http::HttpMatchingData& data) const {
  DataInputContext context;
  context.request_headers = data.requestHeaders().ptr();
  context.response_headers = data.responseHeaders().ptr();
  context.response_trailers = data.responseTrailers().ptr();
  on_get_(in_module_config_.get(), static_cast<void*>(&context));
  if (context.has_result) {
    return ::Envoy::Matcher::DataInputGetResult::CreateString(std::move(context.result));
  }
  return ::Envoy::Matcher::DataInputGetResult::NoData();
}

::Envoy::Matcher::DataInputFactoryCb<::Envoy::Http::HttpMatchingData>
DynamicModuleDataInputFactory::createDataInputFactoryCb(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  const auto& proto_config = MessageUtil::downcastAndValidate<const DynamicModuleDataInputProto&>(
      config, validation_visitor);

  // Data inputs have no factory context, so only the synchronous local-file and by-name module
  // sources can load here. A remote source is rejected by newDynamicModuleByConfig.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), proto_config.input_name());
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }
  auto dynamic_module = std::move(load_result->loaded);

  auto on_config_new = dynamic_module->getFunctionPointer<OnDataInputConfigNewType>(
      "envoy_dynamic_module_on_matcher_data_input_config_new");
  if (!on_config_new.ok()) {
    throw EnvoyException("Failed to resolve symbol: " +
                         std::string(on_config_new.status().message()));
  }

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnDataInputConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_data_input_config_destroy");
  if (!on_config_destroy.ok()) {
    throw EnvoyException("Failed to resolve symbol: " +
                         std::string(on_config_destroy.status().message()));
  }

  auto on_get = dynamic_module->getFunctionPointer<OnDataInputGetType>(
      "envoy_dynamic_module_on_matcher_data_input_get");
  if (!on_get.ok()) {
    throw EnvoyException("Failed to resolve symbol: " + std::string(on_get.status().message()));
  }

  // Parse the data input config.
  std::string input_config_str;
  if (proto_config.has_input_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.input_config());
    if (!config_or_error.ok()) {
      throw EnvoyException("Failed to parse data input config: " +
                           std::string(config_or_error.status().message()));
    }
    input_config_str = std::move(config_or_error.value());
  }

  envoy_dynamic_module_type_envoy_buffer name_buf = {proto_config.input_name().data(),
                                                     proto_config.input_name().size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {input_config_str.data(),
                                                       input_config_str.size()};

  auto in_module_config = (*on_config_new.value())(nullptr, name_buf, config_buf);
  if (in_module_config == nullptr) {
    throw EnvoyException("Failed to initialize dynamic module matcher data input config");
  }

  auto shared_module =
      std::shared_ptr<Extensions::DynamicModules::DynamicModule>(std::move(dynamic_module));

  // Own the in-module configuration in a shared holder so it is destroyed exactly once through
  // on_matcher_data_input_config_destroy, whether the factory callback runs zero, one, or many
  // times. The deleter also holds the module so the destroy hook is never called into an unloaded
  // module.
  std::shared_ptr<const void> shared_config(
      in_module_config, [shared_module, on_config_destroy = on_config_destroy.value()](
                            const void* config) { on_config_destroy(config); });

  return [shared_module, on_get = on_get.value(), shared_config] {
    return std::make_unique<DynamicModuleDataInput>(shared_module, on_get, shared_config);
  };
}

REGISTER_FACTORY(DynamicModuleDataInputFactory,
                 ::Envoy::Matcher::DataInputFactory<::Envoy::Http::HttpMatchingData>);

} // namespace DynamicModules
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

extern "C" {

bool envoy_dynamic_module_callback_matcher_data_input_get_header_value(
    envoy_dynamic_module_type_matcher_data_input_envoy_ptr data_input_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  using namespace Envoy::Extensions::Matching::Http::DynamicModules;
  auto* context = static_cast<DataInputContext*>(data_input_envoy_ptr);
  auto map = headerMapByType(context, header_type);
  if (!map.has_value()) {
    *result = {.ptr = nullptr, .length = 0};
    if (total_count_out != nullptr) {
      *total_count_out = 0;
    }
    return false;
  }
  const auto values =
      map->get(::Envoy::Http::LowerCaseString(absl::string_view(key.ptr, key.length)));
  if (total_count_out != nullptr) {
    *total_count_out = values.size();
  }
  if (index >= values.size()) {
    *result = {.ptr = nullptr, .length = 0};
    return false;
  }
  const auto value = values[index]->value().getStringView();
  *result = {.ptr = value.data(), .length = value.size()};
  return true;
}

void envoy_dynamic_module_callback_matcher_data_input_set_result(
    envoy_dynamic_module_type_matcher_data_input_envoy_ptr data_input_envoy_ptr,
    envoy_dynamic_module_type_module_buffer result) {
  using namespace Envoy::Extensions::Matching::Http::DynamicModules;
  auto* context = static_cast<DataInputContext*>(data_input_envoy_ptr);
  context->result.assign(result.ptr, result.length);
  context->has_result = true;
}

} // extern "C"
