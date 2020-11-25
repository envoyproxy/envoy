#include "common/config/xds_context_params.h"

#include "common/common/macros.h"
#include "common/protobuf/utility.h"

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
  for (const auto& it : metadata.fields()) {
    params[prefix + it.first] = MessageUtil::getJsonStringFromMessage(it.second);
  }
}

} // namespace

xds::core::v3::ContextParams XdsContextParams::encode(
    const envoy::config::core::v3::Node& node, const std::vector<std::string>& node_context_params,
    const xds::core::v3::ContextParams& resource_context_params,
    const std::vector<std::string>& client_features,
    const absl::flat_hash_map<std::string, std::string>& extra_resource_params) {
  xds::core::v3::ContextParams context_params;
  auto& mutable_params = *context_params.mutable_params();
  // 1. Establish base layer of per-node context parameters.
  for (const std::string& ncp : node_context_params) {
    // First attempt field accessors known ahead of time, if that fails we consider the cases of
    // metadata, either directly in the Node message, or nested in the user_agent_build_version.
    if (nodeParamCbs().count(ncp) > 0) {
      mutable_params["xds.node." + ncp] = nodeParamCbs().at(ncp)(node);
    } else if (ncp == "metadata") {
      mergeMetadataJson(mutable_params, node.metadata(), "xds.node.metadata.");
    } else if (ncp == "user_agent_build_version.metadata") {
      mergeMetadataJson(mutable_params, node.user_agent_build_version().metadata(),
                        "xds.node.user_agent_build_version.metadata.");
    }
  }

  // 2. Overlay with context parameters from resource name.
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

} // namespace Config
} // namespace Envoy
