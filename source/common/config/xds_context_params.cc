#include "source/common/config/xds_context_params.h"

#include "source/common/common/macros.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Config {

namespace {

using RenderContextParamCb = std::function<std::string(const envoy::config::core::v3::Node& node)>;
using NodeContextRenderers = absl::flat_hash_map<std::string, RenderContextParamCb>;

RenderContextParamCb directStringFieldRenderer(const std::string& field) {
  return [field](const envoy::config::core::v3::Node& node) -> std::string {
    return MessageUtil::getStringField(node, field);
  };
}

RenderContextParamCb localityStringFieldRenderer(const std::string& field) {
  return [field](const envoy::config::core::v3::Node& node) -> std::string {
    return MessageUtil::getStringField(node.locality(), field);
  };
}

std::string buildSemanticVersionRenderer(const envoy::config::core::v3::Node& node) {
  const auto& semver = node.user_agent_build_version().version();
  return fmt::format("{}.{}.{}", semver.major_number(), semver.minor_number(), semver.patch());
}

const NodeContextRenderers& nodeParamCbs() {
  CONSTRUCT_ON_FIRST_USE(NodeContextRenderers, {"id", directStringFieldRenderer("id")},
                         {"cluster", directStringFieldRenderer("cluster")},
                         {"user_agent_name", directStringFieldRenderer("user_agent_name")},
                         {"user_agent_version", directStringFieldRenderer("user_agent_version")},
                         {"locality.region", localityStringFieldRenderer("region")},
                         {"locality.zone", localityStringFieldRenderer("zone")},
                         {"locality.sub_zone", localityStringFieldRenderer("sub_zone")},
                         {"user_agent_build_version.version", buildSemanticVersionRenderer});
}

void mergeMetadataJson(Protobuf::Map<std::string, std::string>& params,
                       const ProtobufWkt::Struct& metadata, const std::string& prefix) {
#ifdef ENVOY_ENABLE_YAML
  for (const auto& it : metadata.fields()) {
    absl::StatusOr<std::string> json_or_error = MessageUtil::getJsonStringFromMessage(it.second);
    ENVOY_BUG(json_or_error.ok(), "Failed to parse json");
    if (json_or_error.ok()) {
      params[prefix + it.first] = json_or_error.value();
    }
  }
#else
  UNREFERENCED_PARAMETER(params);
  UNREFERENCED_PARAMETER(metadata);
  UNREFERENCED_PARAMETER(prefix);
  IS_ENVOY_BUG("JSON/YAML support compiled out");
#endif
}

} // namespace

xds::core::v3::ContextParams XdsContextParams::encodeResource(
    const xds::core::v3::ContextParams& node_context_params,
    const xds::core::v3::ContextParams& resource_context_params,
    const std::vector<std::string>& client_features,
    const absl::flat_hash_map<std::string, std::string>& extra_resource_params) {
  xds::core::v3::ContextParams context_params;
  // 1. Establish base layer of per-node context parameters.
  context_params.MergeFrom(node_context_params);

  // 2. Overlay with context parameters from resource name.
  auto& mutable_params = *context_params.mutable_params();
  for (const auto& it : resource_context_params.params()) {
    mutable_params[it.first] = it.second;
  }

  // 3. Overlay with per-resource type context parameters.
  for (const std::string& cf : client_features) {
    mutable_params["xds.client_feature." + cf] = "true";
  }

  // 4. Overlay with per-resource well-known attributes.
  for (const auto& it : extra_resource_params) {
    mutable_params["xds.resource." + it.first] = it.second;
  }

  return context_params;
}

xds::core::v3::ContextParams XdsContextParams::encodeNodeContext(
    const envoy::config::core::v3::Node& node,
    const Protobuf::RepeatedPtrField<std::string>& node_context_params) {
  // E.g. if we have a node instance with contents
  //
  // user_agent_build_version:
  //   version:
  //     major_number: 1
  //     minor_number: 2
  //     patch: 3
  //   metadata:
  //     foo: true
  //     bar: "a"
  //     baz: 42
  //
  // and node_context_params [user_agent_build_version.version, user_agent_build_version.metadata],
  // we end up with the map:
  //
  // xds.node.user_agent_build_version.metadata.bar: "\"a\""
  // xds.node.user_agent_build_version.metadata.baz: "42"
  // xds.node.user_agent_build_version.metadata.foo: "true"
  // xds.node.user_agent_build_version.version: "1.2.3"
  xds::core::v3::ContextParams context_params;
  auto& mutable_params = *context_params.mutable_params();
  for (const std::string& ncp : node_context_params) {
    // First attempt field accessors known ahead of time, if that fails we consider the cases of
    // metadata, either directly in the Node message, or nested in the user_agent_build_version.
    auto it = nodeParamCbs().find(ncp);
    if (it != nodeParamCbs().end()) {
      mutable_params["xds.node." + ncp] = (it->second)(node);
    } else if (ncp == "metadata") {
      mergeMetadataJson(mutable_params, node.metadata(), "xds.node.metadata.");
    } else if (ncp == "user_agent_build_version.metadata") {
      mergeMetadataJson(mutable_params, node.user_agent_build_version().metadata(),
                        "xds.node.user_agent_build_version.metadata.");
    }
  }
  return context_params;
}

} // namespace Config
} // namespace Envoy
