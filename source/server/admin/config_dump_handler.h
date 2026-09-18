#pragma once

#include <deque>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/statusor.h"
#include "source/server/admin/handler_ctx.h"

namespace Envoy {
namespace Server {

// A dump is `{"configs":[...]}`, one Any per component: bootstrap, clusters, listeners and the
// rest, each packing that component's ConfigDump message. `?resource=` names a repeated field
// inside one of those messages, `?mask=` trims the fields of its elements.

// A cluster the endpoints component covers. `dynamic_` is set for a cluster added via the API,
// which is dumped under dynamic_endpoint_configs rather than static_endpoint_configs.
struct EndpointCluster {
  std::string name_;
  bool dynamic_;
};

// A single component and the repeated field in it that `?resource=` named.
struct ResourceField {
  ProtobufTypes::MessagePtr config_;
  const Protobuf::FieldDescriptor* field_;
};

class ConfigDumpHandler : public HandlerContextBase {

public:
  ConfigDumpHandler(ConfigTracker& config_tracker, Server::Instance& server);

  Admin::UrlHandler handlerConfigDumpStreamed();

  Admin::RequestPtr makeRequest(AdminStream& admin_stream) const;

private:
  friend class ConfigDumpRequest;

  std::deque<std::string> componentNames(bool include_eds) const;

  bool dumpsEndpoints(const std::string& name, bool include_eds) const;

  /**
   * Helper methods to add endpoints config
   */
  void addLbEndpoint(const Upstream::HostSharedPtr& host,
                     envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) const;

  /**
   * Builds the ClusterLoadAssignment dumped for one cluster.
   * @return ClusterLoadAssignment if `name_matcher` matches the cluster name, nullopt otherwise
   */
  std::optional<envoy::config::endpoint::v3::ClusterLoadAssignment>
  buildClusterLoadAssignment(const Upstream::Cluster& cluster,
                             const Matchers::StringMatcher& name_matcher) const;

  ProtobufTypes::MessagePtr dumpEndpointConfigs(const Matchers::StringMatcher& name_matcher) const;

  std::deque<EndpointCluster>
  snapshotEndpointClusters(const Matchers::StringMatcher& name_matcher) const;

  /**
   * @return the ClusterLoadAssignment or nullptr if that cluster is no longer active
   */
  std::unique_ptr<envoy::config::endpoint::v3::ClusterLoadAssignment>
  dumpClusterLoadAssignment(const EndpointCluster& endpoint_cluster,
                            const Matchers::StringMatcher& name_matcher) const;

  // Dumps the config the ConfigTracker holds under `name`, or nullptr if it holds none.
  ProtobufTypes::MessagePtr trackedConfig(const std::string& name,
                                          const Matchers::StringMatcher& name_matcher) const;

  // The config dumped under `name`, or nullptr if nothing is dumped under it any more.
  ProtobufTypes::MessagePtr dumpComponent(const std::string& name, bool include_eds,
                                          const Matchers::StringMatcher& name_matcher) const;

  /**
   * Finds the component with a repeated field named `resource` and applies `mask` to its elements.
   * Pops the names of the components it dumps looking for it.
   * @return that component and its `resource` field, or the error to answer with.
   */
  absl::StatusOr<ResourceField> findResource(const std::string& resource,
                                             const std::optional<std::string>& mask,
                                             std::deque<std::string>& component_names,
                                             bool include_eds,
                                             const Matchers::StringMatcher& name_matcher) const;

  /**
   * Dumps components, popping their names, until `field_mask` applies to one.
   * @return that component, or nullptr if the mask applied to none of them.
   */
  ProtobufTypes::MessagePtr firstMaskedConfig(const Protobuf::FieldMask& field_mask,
                                              std::deque<std::string>& component_names,
                                              bool include_eds,
                                              const Matchers::StringMatcher& name_matcher) const;

  ConfigTracker& config_tracker_;
};

class ConfigDumpRequest : public Admin::Request {
public:
  static constexpr uint64_t DefaultChunkSize = 64 * 1024;

  struct Dump {
    Matchers::StringMatcherPtr name_matcher_;
    std::deque<std::string> component_names_;
    bool include_eds_{false};
    std::optional<Protobuf::FieldMask> field_mask_;
    ProtobufTypes::MessagePtr pending_config_;
    std::optional<ResourceField> resource_;
  };

  ConfigDumpRequest(const ConfigDumpHandler& handler, Dump dump);
  ~ConfigDumpRequest() override;

  // Admin::Request
  Http::Code start(Http::ResponseHeaderMap& response_headers) override;
  bool nextChunk(Buffer::Instance& response) override;

  void setChunkSize(uint64_t chunk_size) { chunk_size_ = chunk_size; }

private:
  class Document;

  bool serializeNextConfig();

  bool serializeNextResourceElement();

  // Dumps components until the mask applies to one, or nullptr once none are left.
  ProtobufTypes::MessagePtr nextMaskedConfig();

  // Emits one cluster of the endpoints component, closing it once none are left.
  void serializeNextEndpoint();

  const ConfigDumpHandler& handler_;
  Dump dump_;
  Buffer::OwnedImpl response_;
  std::unique_ptr<Document> document_;
  int next_resource_element_{0};
  // The clusters whose endpoints are left to emit
  std::deque<EndpointCluster> endpoint_clusters_;
  uint64_t chunk_size_{DefaultChunkSize};
};

} // namespace Server
} // namespace Envoy
