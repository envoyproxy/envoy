#pragma once

#include "envoy/common/hashable.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Istio {
namespace Common {

// Filter state key to store the peer metadata under.
constexpr absl::string_view DownstreamPeer = "downstream_peer";
constexpr absl::string_view UpstreamPeer = "upstream_peer";

// Special filter state key to indicate the filter is done looking for peer metadata.
// This is used by network metadata exchange on failure.
constexpr absl::string_view NoPeer = "peer_not_found";

// Special labels used in the peer metadata.
constexpr absl::string_view CanonicalNameLabel = "service.istio.io/canonical-name";
constexpr absl::string_view CanonicalRevisionLabel = "service.istio.io/canonical-revision";
constexpr absl::string_view AppNameLabel = "app";
constexpr absl::string_view AppVersionLabel = "version";

enum class WorkloadType {
  Unknown,
  Pod,
  Deployment,
  Job,
  CronJob,
};

constexpr absl::string_view OwnerPrefix = "kubernetes://apis/apps/v1/namespaces/";

constexpr absl::string_view PodSuffix = "pod";
constexpr absl::string_view DeploymentSuffix = "deployment";
constexpr absl::string_view JobSuffix = "job";
constexpr absl::string_view CronJobSuffix = "cronjob";

enum class BaggageToken {
  NamespaceName,
  ClusterName,
  ServiceName,
  ServiceVersion,
  AppName,
  AppVersion,
  WorkloadName,
  WorkloadType,
  InstanceName,
};

constexpr absl::string_view NamespaceNameToken = "namespace";
constexpr absl::string_view ClusterNameToken = "cluster";
constexpr absl::string_view ServiceNameToken = "service";
constexpr absl::string_view ServiceVersionToken = "revision";
constexpr absl::string_view AppNameToken = "app";
constexpr absl::string_view AppVersionToken = "version";
constexpr absl::string_view WorkloadNameToken = "workload";
constexpr absl::string_view WorkloadTypeToken = "type";
constexpr absl::string_view InstanceNameToken = "name";
constexpr absl::string_view LabelsToken = "labels";
constexpr absl::string_view IdentityToken = "identity";

constexpr absl::string_view InstanceMetadataField = "NAME";
constexpr absl::string_view NamespaceMetadataField = "NAMESPACE";
constexpr absl::string_view ClusterMetadataField = "CLUSTER_ID";
constexpr absl::string_view OwnerMetadataField = "OWNER";
constexpr absl::string_view WorkloadMetadataField = "WORKLOAD_NAME";
constexpr absl::string_view LabelsMetadataField = "LABELS";

class WorkloadMetadataObject : public Envoy::StreamInfo::FilterState::Object,
                               public Envoy::Hashable {
public:
  explicit WorkloadMetadataObject(absl::string_view instance_name, absl::string_view cluster_name,
                                  absl::string_view namespace_name, absl::string_view workload_name,
                                  absl::string_view canonical_name,
                                  absl::string_view canonical_revision, absl::string_view app_name,
                                  absl::string_view app_version, WorkloadType workload_type,
                                  absl::string_view identity)
      : instance_name_(instance_name), cluster_name_(cluster_name), namespace_name_(namespace_name),
        workload_name_(workload_name), canonical_name_(canonical_name),
        canonical_revision_(canonical_revision), app_name_(app_name), app_version_(app_version),
        workload_type_(workload_type), identity_(identity) {}

  absl::optional<uint64_t> hash() const override;
  Envoy::ProtobufTypes::MessagePtr serializeAsProto() const override;
  std::vector<std::pair<absl::string_view, absl::string_view>> serializeAsPairs() const;
  absl::optional<std::string> serializeAsString() const override;
  absl::optional<std::string> owner() const;
  bool hasFieldSupport() const override { return true; }
  using Envoy::StreamInfo::FilterState::Object::FieldType;
  FieldType getField(absl::string_view) const override;
  void setLabels(std::vector<std::pair<std::string, std::string>> labels) { labels_ = labels; }
  std::vector<std::pair<std::string, std::string>> getLabels() const { return labels_; }

  const std::string instance_name_;
  const std::string cluster_name_;
  const std::string namespace_name_;
  const std::string workload_name_;
  const std::string canonical_name_;
  const std::string canonical_revision_;
  const std::string app_name_;
  const std::string app_version_;
  const WorkloadType workload_type_;
  const std::string identity_;
  std::vector<std::pair<std::string, std::string>> labels_;
};

// Parse string workload type.
WorkloadType fromSuffix(absl::string_view suffix);

// Parse owner field from kubernetes to detect the workload type.
WorkloadType parseOwner(absl::string_view owner, absl::string_view workload);

// Convert a metadata object to a struct.
Envoy::Protobuf::Struct convertWorkloadMetadataToStruct(const WorkloadMetadataObject& obj);

// Convert struct to a metadata object.
std::unique_ptr<WorkloadMetadataObject>
convertStructToWorkloadMetadata(const Envoy::Protobuf::Struct& metadata);

std::unique_ptr<WorkloadMetadataObject>
convertStructToWorkloadMetadata(const Envoy::Protobuf::Struct& metadata,
                                const absl::flat_hash_set<std::string>& additional_labels);

// Convert endpoint metadata string to a metadata object.
// Telemetry metadata is compressed into a semicolon separated string:
// workload-name;namespace;canonical-service-name;canonical-service-revision;cluster-id.
// Telemetry metadata is stored as a string under "istio", "workload" field
// path.
absl::optional<WorkloadMetadataObject>
convertEndpointMetadata(const std::string& endpoint_encoding);

std::string serializeToStringDeterministic(const Envoy::Protobuf::Struct& metadata);

// Convert from baggage encoding.
std::unique_ptr<WorkloadMetadataObject> convertBaggageToWorkloadMetadata(absl::string_view data);

} // namespace Common
} // namespace Istio
} // namespace Envoy
