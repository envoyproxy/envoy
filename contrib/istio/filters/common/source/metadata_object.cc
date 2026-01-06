#include "contrib/istio/filters/common/source/metadata_object.h"

#include "envoy/registry/registry.h"

#include "source/common/common/hash.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Istio {
namespace Common {

namespace {
static absl::flat_hash_map<absl::string_view, BaggageToken> ALL_BAGGAGE_TOKENS = {
    {NamespaceNameToken, BaggageToken::NamespaceName},
    {ClusterNameToken, BaggageToken::ClusterName},
    {ServiceNameToken, BaggageToken::ServiceName},
    {ServiceVersionToken, BaggageToken::ServiceVersion},
    {AppNameToken, BaggageToken::AppName},
    {AppVersionToken, BaggageToken::AppVersion},
    {WorkloadNameToken, BaggageToken::WorkloadName},
    {WorkloadTypeToken, BaggageToken::WorkloadType},
    {InstanceNameToken, BaggageToken::InstanceName},
};

static absl::flat_hash_map<absl::string_view, WorkloadType> ALL_WORKLOAD_TOKENS = {
    {PodSuffix, WorkloadType::Pod},
    {DeploymentSuffix, WorkloadType::Deployment},
    {JobSuffix, WorkloadType::Job},
    {CronJobSuffix, WorkloadType::CronJob},
};

absl::optional<absl::string_view> toSuffix(WorkloadType workload_type) {
  switch (workload_type) {
  case WorkloadType::Deployment:
    return DeploymentSuffix;
  case WorkloadType::CronJob:
    return CronJobSuffix;
  case WorkloadType::Job:
    return JobSuffix;
  case WorkloadType::Pod:
    return PodSuffix;
  case WorkloadType::Unknown:
  default:
    return {};
  }
}

} // namespace

Envoy::ProtobufTypes::MessagePtr WorkloadMetadataObject::serializeAsProto() const {
  auto message = std::make_unique<Envoy::Protobuf::Struct>();
  const auto suffix = toSuffix(workload_type_);
  if (suffix) {
    (*message->mutable_fields())[WorkloadTypeToken].set_string_value(*suffix);
  }
  if (!workload_name_.empty()) {
    (*message->mutable_fields())[WorkloadNameToken].set_string_value(workload_name_);
  }
  if (!cluster_name_.empty()) {
    (*message->mutable_fields())[InstanceNameToken].set_string_value(instance_name_);
  }
  if (!cluster_name_.empty()) {
    (*message->mutable_fields())[ClusterNameToken].set_string_value(cluster_name_);
  }
  if (!namespace_name_.empty()) {
    (*message->mutable_fields())[NamespaceNameToken].set_string_value(namespace_name_);
  }
  if (!canonical_name_.empty()) {
    (*message->mutable_fields())[ServiceNameToken].set_string_value(canonical_name_);
  }
  if (!canonical_revision_.empty()) {
    (*message->mutable_fields())[ServiceVersionToken].set_string_value(canonical_revision_);
  }
  if (!app_name_.empty()) {
    (*message->mutable_fields())[AppNameToken].set_string_value(app_name_);
  }
  if (!app_version_.empty()) {
    (*message->mutable_fields())[AppVersionToken].set_string_value(app_version_);
  }
  if (!identity_.empty()) {
    (*message->mutable_fields())[IdentityToken].set_string_value(identity_);
  }

  if (!labels_.empty()) {
    auto* labels = (*message->mutable_fields())[LabelsToken].mutable_struct_value();
    for (const auto& l : labels_) {
      (*labels->mutable_fields())[std::string(l.first)].set_string_value(std::string(l.second));
    }
  }

  return message;
}

std::vector<std::pair<absl::string_view, absl::string_view>>
WorkloadMetadataObject::serializeAsPairs() const {
  std::vector<std::pair<absl::string_view, absl::string_view>> parts;
  const auto suffix = toSuffix(workload_type_);
  if (suffix) {
    parts.push_back({WorkloadTypeToken, *suffix});
  }
  if (!workload_name_.empty()) {
    parts.push_back({WorkloadNameToken, workload_name_});
  }
  if (!instance_name_.empty()) {
    parts.push_back({InstanceNameToken, instance_name_});
  }
  if (!cluster_name_.empty()) {
    parts.push_back({ClusterNameToken, cluster_name_});
  }
  if (!namespace_name_.empty()) {
    parts.push_back({NamespaceNameToken, namespace_name_});
  }
  if (!canonical_name_.empty()) {
    parts.push_back({ServiceNameToken, canonical_name_});
  }
  if (!canonical_revision_.empty()) {
    parts.push_back({ServiceVersionToken, canonical_revision_});
  }
  if (!app_name_.empty()) {
    parts.push_back({AppNameToken, app_name_});
  }
  if (!app_version_.empty()) {
    parts.push_back({AppVersionToken, app_version_});
  }
  if (!labels_.empty()) {
    for (const auto& l : labels_) {
      parts.push_back({absl::StrCat("labels[]", l.first), absl::string_view(l.second)});
    }
  }
  return parts;
}

absl::optional<std::string> WorkloadMetadataObject::serializeAsString() const {
  const auto parts = serializeAsPairs();
  return absl::StrJoin(parts, ",", absl::PairFormatter("="));
}

absl::optional<uint64_t> WorkloadMetadataObject::hash() const {
  return Envoy::HashUtil::xxHash64(*serializeAsString());
}

absl::optional<std::string> WorkloadMetadataObject::owner() const {
  const auto suffix = toSuffix(workload_type_);
  if (suffix) {
    return absl::StrCat(OwnerPrefix, namespace_name_, "/", *suffix, "s/", workload_name_);
  }
  return {};
}

WorkloadType fromSuffix(absl::string_view suffix) {
  const auto it = ALL_WORKLOAD_TOKENS.find(suffix);
  if (it != ALL_WORKLOAD_TOKENS.end()) {
    return it->second;
  }
  return WorkloadType::Unknown;
}

WorkloadType parseOwner(absl::string_view owner, absl::string_view workload) {
  // Strip "s/workload_name" and check for workload type.
  if (owner.size() > workload.size() + 2) {
    owner.remove_suffix(workload.size() + 2);
    size_t last = owner.rfind('/');
    if (last != absl::string_view::npos) {
      return fromSuffix(owner.substr(last + 1));
    }
  }
  return WorkloadType::Unknown;
}

Envoy::Protobuf::Struct convertWorkloadMetadataToStruct(const WorkloadMetadataObject& obj) {
  Envoy::Protobuf::Struct metadata;
  if (!obj.instance_name_.empty()) {
    (*metadata.mutable_fields())[InstanceMetadataField].set_string_value(obj.instance_name_);
  }
  if (!obj.namespace_name_.empty()) {
    (*metadata.mutable_fields())[NamespaceMetadataField].set_string_value(obj.namespace_name_);
  }
  if (!obj.workload_name_.empty()) {
    (*metadata.mutable_fields())[WorkloadMetadataField].set_string_value(obj.workload_name_);
  }
  if (!obj.cluster_name_.empty()) {
    (*metadata.mutable_fields())[ClusterMetadataField].set_string_value(obj.cluster_name_);
  }
  auto* labels = (*metadata.mutable_fields())[LabelsMetadataField].mutable_struct_value();
  if (!obj.canonical_name_.empty()) {
    (*labels->mutable_fields())[CanonicalNameLabel].set_string_value(obj.canonical_name_);
  }
  if (!obj.canonical_revision_.empty()) {
    (*labels->mutable_fields())[CanonicalRevisionLabel].set_string_value(obj.canonical_revision_);
  }
  if (!obj.app_name_.empty()) {
    (*labels->mutable_fields())[AppNameLabel].set_string_value(obj.app_name_);
  }
  if (!obj.app_version_.empty()) {
    (*labels->mutable_fields())[AppVersionLabel].set_string_value(obj.app_version_);
  }
  if (!obj.getLabels().empty()) {
    for (const auto& lbl : obj.getLabels()) {
      (*labels->mutable_fields())[std::string(lbl.first)].set_string_value(std::string(lbl.second));
    }
  }
  if (const auto owner = obj.owner(); owner.has_value()) {
    (*metadata.mutable_fields())[OwnerMetadataField].set_string_value(*owner);
  }
  return metadata;
}

// Convert struct to a metadata object.
std::unique_ptr<WorkloadMetadataObject>
convertStructToWorkloadMetadata(const Envoy::Protobuf::Struct& metadata) {
  return convertStructToWorkloadMetadata(metadata, {});
}

std::unique_ptr<WorkloadMetadataObject>
convertStructToWorkloadMetadata(const Envoy::Protobuf::Struct& metadata,
                                const absl::flat_hash_set<std::string>& additional_labels) {
  absl::string_view instance, namespace_name, owner, workload, cluster, canonical_name,
      canonical_revision, app_name, app_version;
  std::vector<std::pair<std::string, std::string>> labels;
  for (const auto& it : metadata.fields()) {
    if (it.first == InstanceMetadataField) {
      instance = it.second.string_value();
    } else if (it.first == NamespaceMetadataField) {
      namespace_name = it.second.string_value();
    } else if (it.first == OwnerMetadataField) {
      owner = it.second.string_value();
    } else if (it.first == WorkloadMetadataField) {
      workload = it.second.string_value();
    } else if (it.first == ClusterMetadataField) {
      cluster = it.second.string_value();
    } else if (it.first == LabelsMetadataField) {
      for (const auto& labels_it : it.second.struct_value().fields()) {
        if (labels_it.first == CanonicalNameLabel) {
          canonical_name = labels_it.second.string_value();
        } else if (labels_it.first == CanonicalRevisionLabel) {
          canonical_revision = labels_it.second.string_value();
        } else if (labels_it.first == AppNameLabel) {
          app_name = labels_it.second.string_value();
        } else if (labels_it.first == AppVersionLabel) {
          app_version = labels_it.second.string_value();
        } else if (!additional_labels.empty() &&
                   additional_labels.contains(std::string(labels_it.first))) {
          labels.push_back(
              {std::string(labels_it.first), std::string(labels_it.second.string_value())});
        }
      }
    }
  }
  auto obj = std::make_unique<WorkloadMetadataObject>(instance, cluster, namespace_name, workload,
                                                      canonical_name, canonical_revision, app_name,
                                                      app_version, parseOwner(owner, workload), "");
  obj->setLabels(labels);
  return obj;
}

absl::optional<WorkloadMetadataObject>
convertEndpointMetadata(const std::string& endpoint_encoding) {
  std::vector<absl::string_view> parts = absl::StrSplit(endpoint_encoding, ';');
  if (parts.size() < 5) {
    return {};
  }
  return absl::make_optional<WorkloadMetadataObject>("", parts[4], parts[1], parts[0], parts[2],
                                                     parts[3], "", "", WorkloadType::Unknown, "");
}

std::string serializeToStringDeterministic(const Envoy::Protobuf::Struct& metadata) {
  std::string out;
  {
    Envoy::Protobuf::io::StringOutputStream md(&out);
    Envoy::Protobuf::io::CodedOutputStream mcs(&md);
    mcs.SetSerializationDeterministic(true);
    if (!metadata.SerializeToCodedStream(&mcs)) {
      out.clear();
    }
  }
  return out;
}

WorkloadMetadataObject::FieldType
WorkloadMetadataObject::getField(absl::string_view field_name) const {
  const auto it = ALL_BAGGAGE_TOKENS.find(field_name);
  if (it != ALL_BAGGAGE_TOKENS.end()) {
    switch (it->second) {
    case BaggageToken::NamespaceName:
      return namespace_name_;
    case BaggageToken::ClusterName:
      return cluster_name_;
    case BaggageToken::ServiceName:
      return canonical_name_;
    case BaggageToken::ServiceVersion:
      return canonical_revision_;
    case BaggageToken::AppName:
      return app_name_;
    case BaggageToken::AppVersion:
      return app_version_;
    case BaggageToken::WorkloadName:
      return workload_name_;
    case BaggageToken::WorkloadType:
      if (const auto value = toSuffix(workload_type_); value.has_value()) {
        return *value;
      }
      return "unknown";
    case BaggageToken::InstanceName:
      return instance_name_;
    }
  }
  return {};
}

std::unique_ptr<WorkloadMetadataObject> convertBaggageToWorkloadMetadata(absl::string_view data) {
  absl::string_view instance;
  absl::string_view cluster;
  absl::string_view workload;
  absl::string_view namespace_name;
  absl::string_view canonical_name;
  absl::string_view canonical_revision;
  absl::string_view app_name;
  absl::string_view app_version;
  WorkloadType workload_type = WorkloadType::Unknown;
  std::vector<absl::string_view> properties = absl::StrSplit(data, ',');
  for (absl::string_view property : properties) {
    std::pair<absl::string_view, absl::string_view> parts = absl::StrSplit(property, '=');
    const auto it = ALL_BAGGAGE_TOKENS.find(parts.first);
    if (it != ALL_BAGGAGE_TOKENS.end()) {
      switch (it->second) {
      case BaggageToken::NamespaceName:
        namespace_name = parts.second;
        break;
      case BaggageToken::ClusterName:
        cluster = parts.second;
        break;
      case BaggageToken::ServiceName:
        canonical_name = parts.second;
        break;
      case BaggageToken::ServiceVersion:
        canonical_revision = parts.second;
        break;
      case BaggageToken::AppName:
        app_name = parts.second;
        break;
      case BaggageToken::AppVersion:
        app_version = parts.second;
        break;
      case BaggageToken::WorkloadName:
        workload = parts.second;
        break;
      case BaggageToken::WorkloadType:
        workload_type = fromSuffix(parts.second);
        break;
      case BaggageToken::InstanceName:
        instance = parts.second;
        break;
      }
    }
  }
  return std::make_unique<WorkloadMetadataObject>(instance, cluster, namespace_name, workload,
                                                  canonical_name, canonical_revision, app_name,
                                                  app_version, workload_type, "");
}

} // namespace Common
} // namespace Istio
} // namespace Envoy
