#pragma once

#include "envoy/common/hashable.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Istio {
namespace Common {

// Filter state keys to store the peer metadata under.
// CelState is stored under these keys for CEL expression support.
constexpr absl::string_view DownstreamPeer = "downstream_peer";
constexpr absl::string_view UpstreamPeer = "upstream_peer";

// Filter state keys for WorkloadMetadataObject (FIELD accessor support).
constexpr absl::string_view DownstreamPeerObj = "downstream_peer_obj";
constexpr absl::string_view UpstreamPeerObj = "upstream_peer_obj";

// Special filter state key to indicate the filter is done looking for peer metadata.
// This is used by network metadata exchange on failure.
constexpr absl::string_view NoPeer = "peer_not_found";

// Special labels used in the peer metadata.
constexpr absl::string_view CanonicalNameLabel = "service.istio.io/canonical-name";
constexpr absl::string_view CanonicalRevisionLabel = "service.istio.io/canonical-revision";
constexpr absl::string_view AppNameQualifiedLabel = "app.kubernetes.io/name";
constexpr absl::string_view AppNameLabel = "app";
constexpr absl::string_view AppVersionQualifiedLabel = "app.kubernetes.io/version";
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
  LocalityZone,
  LocalityRegion,
};

// Field names accessible from WorkloadMetadataObject.
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
constexpr absl::string_view RegionToken = "region";
constexpr absl::string_view ZoneToken = "availability_zone";

// Field names used to translate baggage content into WorkloadMetadataObject information.
constexpr absl::string_view NamespaceNameBaggageToken = "k8s.namespace.name";
constexpr absl::string_view ClusterNameBaggageToken = "k8s.cluster.name";
constexpr absl::string_view ServiceNameBaggageToken = "service.name";
constexpr absl::string_view ServiceVersionBaggageToken = "service.version";
constexpr absl::string_view AppNameBaggageToken = "app.name";
constexpr absl::string_view AppVersionBaggageToken = "app.version";
constexpr absl::string_view DeploymentNameBaggageToken = "k8s.deployment.name";
constexpr absl::string_view PodNameBaggageToken = "k8s.pod.name";
constexpr absl::string_view CronjobNameBaggageToken = "k8s.cronjob.name";
constexpr absl::string_view JobNameBaggageToken = "k8s.job.name";
constexpr absl::string_view InstanceNameBaggageToken = "k8s.instance.name";
constexpr absl::string_view LocalityRegionBaggageToken = "cloud.region";
constexpr absl::string_view LocalityZoneBaggageToken = "cloud.availability_zone";

constexpr absl::string_view InstanceMetadataField = "NAME";
constexpr absl::string_view NamespaceMetadataField = "NAMESPACE";
constexpr absl::string_view ClusterMetadataField = "CLUSTER_ID";
constexpr absl::string_view IdentityMetadataField = "IDENTITY";
constexpr absl::string_view OwnerMetadataField = "OWNER";
constexpr absl::string_view WorkloadMetadataField = "WORKLOAD_NAME";
constexpr absl::string_view LabelsMetadataField = "LABELS";
constexpr absl::string_view RegionMetadataField = "REGION";
constexpr absl::string_view ZoneMetadataField = "AVAILABILITY_ZONE";

class WorkloadMetadataObject : public Envoy::StreamInfo::FilterState::Object,
                               public Envoy::Hashable {
public:
  explicit WorkloadMetadataObject(absl::string_view instance_name, absl::string_view cluster_name,
                                  absl::string_view namespace_name, absl::string_view workload_name,
                                  absl::string_view canonical_name,
                                  absl::string_view canonical_revision, absl::string_view app_name,
                                  absl::string_view app_version, WorkloadType workload_type,
                                  absl::string_view identity, absl::string_view region = "",
                                  absl::string_view zone = "")
      : instance_name_(instance_name), cluster_name_(cluster_name), namespace_name_(namespace_name),
        workload_name_(workload_name), canonical_name_(canonical_name),
        canonical_revision_(canonical_revision), app_name_(app_name), app_version_(app_version),
        workload_type_(workload_type), identity_(identity), locality_region_(region),
        locality_zone_(zone) {}

  absl::optional<uint64_t> hash() const override;
  Envoy::ProtobufTypes::MessagePtr serializeAsProto() const override;
  std::vector<std::pair<absl::string_view, absl::string_view>> serializeAsPairs() const;
  absl::optional<std::string> serializeAsString() const override;
  absl::optional<std::string> owner() const;
  std::string identity() const;
  bool hasFieldSupport() const override { return true; }
  using Envoy::StreamInfo::FilterState::Object::FieldType;
  FieldType getField(absl::string_view) const override;
  absl::optional<absl::string_view> field(absl::string_view field_name) const;
  void setLabels(std::vector<std::pair<std::string, std::string>> labels) { labels_ = labels; }
  std::vector<std::pair<std::string, std::string>> getLabels() const { return labels_; }
  std::string baggage() const;

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
  const std::string locality_region_;
  const std::string locality_zone_;
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

std::unique_ptr<WorkloadMetadataObject>
convertStructToWorkloadMetadata(const Envoy::Protobuf::Struct& metadata,
                                const absl::flat_hash_set<std::string>& additional_labels,
                                const absl::optional<envoy::config::core::v3::Locality> locality);

// Convert endpoint metadata string to a metadata object.
// Telemetry metadata is compressed into a semicolon separated string:
// workload-name;namespace;canonical-service-name;canonical-service-revision;cluster-id.
// Telemetry metadata is stored as a string under "istio", "workload" field path.
absl::optional<WorkloadMetadataObject>
convertEndpointMetadata(const std::string& endpoint_encoding);

std::string serializeToStringDeterministic(const Envoy::Protobuf::Struct& metadata);

// Convert from baggage encoding.
std::unique_ptr<WorkloadMetadataObject> convertBaggageToWorkloadMetadata(absl::string_view data);
std::unique_ptr<WorkloadMetadataObject>
convertBaggageToWorkloadMetadata(absl::string_view baggage, absl::string_view identity);

} // namespace Common
} // namespace Istio
} // namespace Envoy
