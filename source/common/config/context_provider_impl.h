#pragma once

#include "envoy/config/context_provider.h"

#include "common/common/callback_impl.h"
#include "common/config/xds_context_params.h"

namespace Envoy {
namespace Config {

class ContextProviderImpl : public ContextProvider {
public:
  ContextProviderImpl(const envoy::config::core::v3::Node& node,
                      const Protobuf::RepeatedPtrField<std::string>& node_context_params)
      : node_context_(XdsContextParams::encodeNodeContext(node, node_context_params)) {}

  // Config::ContextProvider
  const xds::core::v3::ContextParams& nodeContext() const override { return node_context_; }
  const xds::core::v3::ContextParams&
  dynamicContext(absl::string_view resource_type_url) const override {
    auto it = dynamic_context_.find(resource_type_url);
    if (it != dynamic_context_.end()) {
      return it->second;
    }
    return xds::core::v3::ContextParams::default_instance();
  };
  void setDynamicContextParam(absl::string_view resource_type_url, absl::string_view key,
                              absl::string_view value) override {
    (*dynamic_context_[resource_type_url].mutable_params())[key] = value;
    update_cb_helper_.runCallbacks(resource_type_url);
  }
  void unsetDynamicContextParam(absl::string_view resource_type_url,
                                absl::string_view key) override {
    dynamic_context_[resource_type_url].mutable_params()->erase(key);
    update_cb_helper_.runCallbacks(resource_type_url);
  }
  Common::CallbackHandle* addDynamicContextUpdateCallback(UpdateCb callback) const override {
    return update_cb_helper_.add(callback);
  };

private:
  const xds::core::v3::ContextParams node_context_;
  // Map from resource type URL to dynamic context parameters.
  absl::flat_hash_map<std::string, xds::core::v3::ContextParams> dynamic_context_;
  mutable Common::CallbackManager<absl::string_view> update_cb_helper_;
};

} // namespace Config
} // namespace Envoy
