#include "contrib/istio/filters/common/source/metadata_object.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/hash.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Istio {
namespace Common {

namespace {

// This maps field names into baggage tokens. We use it to decode field names
// when WorkloadMetadataObject content is accessed through the Envoy API.
static absl::flat_hash_map<absl::string_view, BaggageToken> ALL_METADATA_FIELDS = {
    {NamespaceNameToken, BaggageToken::NamespaceName},
    {ClusterNameToken, BaggageToken::ClusterName},
    {ServiceNameToken, BaggageToken::ServiceName},
    {ServiceVersionToken, BaggageToken::ServiceVersion},
    {AppNameToken, BaggageToken::AppName},
    {AppVersionToken, BaggageToken::AppVersion},
    {WorkloadNameToken, BaggageToken::WorkloadName},
    {WorkloadTypeToken, BaggageToken::WorkloadType},
    {InstanceNameToken, BaggageToken::InstanceName},
    {RegionToken, BaggageToken::LocalityRegion},
    {ZoneToken, BaggageToken::LocalityZone},
};

// This maps baggage keys into baggage tokens. We use it to decode baggage keys
// coming over the wire when building WorkloadMetadataObject.
static absl::flat_hash_map<absl::string_view, BaggageToken> ALL_BAGGAGE_TOKENS = {
    {NamespaceNameBaggageToken, BaggageToken::NamespaceName},
    {ClusterNameBaggageToken, BaggageToken::ClusterName},
    {ServiceNameBaggageToken, BaggageToken::ServiceName},
    {ServiceVersionBaggageToken, BaggageToken::ServiceVersion},
    {AppNameBaggageToken, BaggageToken::AppName},
    {AppVersionBaggageToken, BaggageToken::AppVersion},
    {DeploymentNameBaggageToken, BaggageToken::WorkloadName},
    {PodNameBaggageToken, BaggageToken::WorkloadName},
    {CronjobNameBaggageToken, BaggageToken::WorkloadName},
    {JobNameBaggageToken, BaggageToken::WorkloadName},
    {InstanceNameBaggageToken, BaggageToken::InstanceName},
    {LocalityRegionBaggageToken, BaggageToken::LocalityRegion},
    {LocalityZoneBaggageToken, BaggageToken::LocalityZone},
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

std::string WorkloadMetadataObject::baggage() const {
  const auto workload_type = toSuffix(workload_type_).value_or(PodSuffix);
  std::vector<std::string> parts;
  if (!workload_name_.empty()) {
    parts.push_back("k8s." + std::string(workload_type) + ".name=" + std::string(workload_name_));
  }

  const auto appName = field(AppNameToken).value_or("");
  const auto serviceName = field(ServiceNameToken).value_or(appName);

  if (!serviceName.empty()) {
    parts.push_back(absl::StrCat(ServiceNameBaggageToken, "=", serviceName));
  }

  if (!appName.empty() && appName != serviceName) {
    parts.push_back(absl::StrCat(AppNameBaggageToken, "=", appName));
  }

  const auto appVersion = field(AppVersionToken).value_or("");
  const auto serviceVersion = field(ServiceVersionToken).value_or(appVersion);

  if (!serviceVersion.empty()) {
    parts.push_back(absl::StrCat(ServiceVersionBaggageToken, "=", serviceVersion));
  }

  if (!appVersion.empty() && appVersion != serviceVersion) {
    parts.push_back(absl::StrCat(AppVersionBaggageToken, "=", appVersion));
  }

  // Map the workload metadata fields to baggage tokens
  const std::vector<std::pair<absl::string_view, absl::string_view>> field_to_baggage = {
      {NamespaceNameToken, NamespaceNameBaggageToken}, {ClusterNameToken, ClusterNameBaggageToken},
      {InstanceNameToken, InstanceNameBaggageToken},   {RegionToken, LocalityRegionBaggageToken},
      {ZoneToken, LocalityZoneBaggageToken},
  };

  for (const auto& [field_name, baggage_key] : field_to_baggage) {
    const auto value = field(field_name);
    if (value && !value->empty()) {
      parts.push_back(absl::StrCat(baggage_key, "=", *value));
    }
  }
  return absl::StrJoin(parts, ",");
}

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
  if (!locality_region_.empty()) {
    (*message->mutable_fields())[RegionToken].set_string_value(locality_region_);
  }
  if (!locality_zone_.empty()) {
    (*message->mutable_fields())[ZoneToken].set_string_value(locality_zone_);
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
  if (!locality_region_.empty()) {
    parts.push_back({RegionToken, locality_region_});
  }
  if (!locality_zone_.empty()) {
    parts.push_back({ZoneToken, locality_zone_});
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

std::string WorkloadMetadataObject::identity() const { return identity_; }

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
  if (!obj.identity_.empty()) {
    (*metadata.mutable_fields())[IdentityMetadataField].set_string_value(obj.identity_);
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
  if (!obj.locality_region_.empty()) {
    (*metadata.mutable_fields())[RegionMetadataField].set_string_value(obj.locality_region_);
  }
  if (!obj.locality_zone_.empty()) {
    (*metadata.mutable_fields())[ZoneMetadataField].set_string_value(obj.locality_zone_);
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
  return convertStructToWorkloadMetadata(metadata, additional_labels, {});
}

std::unique_ptr<WorkloadMetadataObject>
convertStructToWorkloadMetadata(const Envoy::Protobuf::Struct& metadata,
                                const absl::flat_hash_set<std::string>& additional_labels,
                                const absl::optional<envoy::config::core::v3::Locality> locality) {
  absl::string_view instance, namespace_name, owner, workload, cluster, identity, canonical_name,
      canonical_revision, app_name, app_version, region, zone;
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
    } else if (it.first == IdentityMetadataField) {
      identity = it.second.string_value();
    } else if (it.first == RegionMetadataField) {
      // This (and zone below) are for the case where locality is propagated
      // via a downstream MX header. For propagation, the locality is passed
      // via the locality argument and these fields shouldn't be used, but
      // we have no way to distinguish locality when we just get a header.
      region = it.second.string_value();
    } else if (it.first == ZoneMetadataField) {
      zone = it.second.string_value();
    } else if (it.first == LabelsMetadataField) {
      for (const auto& labels_it : it.second.struct_value().fields()) {
        if (labels_it.first == CanonicalNameLabel) {
          canonical_name = labels_it.second.string_value();
        } else if (labels_it.first == CanonicalRevisionLabel) {
          canonical_revision = labels_it.second.string_value();
        } else if (labels_it.first == AppNameQualifiedLabel) {
          app_name = labels_it.second.string_value();
        } else if (labels_it.first == AppNameLabel && app_name.empty()) {
          app_name = labels_it.second.string_value();
        } else if (labels_it.first == AppVersionQualifiedLabel) {
          app_version = labels_it.second.string_value();
        } else if (labels_it.first == AppVersionLabel && app_version.empty()) {
          app_version = labels_it.second.string_value();
        } else if (!additional_labels.empty() &&
                   additional_labels.contains(std::string(labels_it.first))) {
          labels.push_back(
              {std::string(labels_it.first), std::string(labels_it.second.string_value())});
        }
      }
    }
  }
  std::string locality_region = std::string(region);
  std::string locality_zone = std::string(zone);
  if (locality.has_value()) {
    if (!locality->region().empty() && locality_region.empty()) {
      locality_region = locality->region();
    }
    if (!locality->zone().empty() && locality_zone.empty()) {
      locality_zone = locality->zone();
    }
  }
  auto obj = std::make_unique<WorkloadMetadataObject>(
      instance, cluster, namespace_name, workload, canonical_name, canonical_revision, app_name,
      app_version, parseOwner(owner, workload), identity, locality_region, locality_zone);
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
                                                     parts[3], "", "", WorkloadType::Unknown, "",
                                                     "", "");
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

absl::optional<absl::string_view>
WorkloadMetadataObject::field(absl::string_view field_name) const {
  const auto it = ALL_METADATA_FIELDS.find(field_name);
  if (it != ALL_METADATA_FIELDS.end()) {
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
    case BaggageToken::LocalityRegion:
      return locality_region_;
    case BaggageToken::LocalityZone:
      return locality_zone_;
    }
  }
  return absl::nullopt;
}

WorkloadMetadataObject::FieldType
WorkloadMetadataObject::getField(absl::string_view field_name) const {
  const auto value = field(field_name);
  if (value) {
    return *value;
  }
  return {};
}

std::unique_ptr<WorkloadMetadataObject>
convertBaggageToWorkloadMetadata(absl::string_view baggage) {
  return convertBaggageToWorkloadMetadata(baggage, "");
}

std::unique_ptr<WorkloadMetadataObject>
convertBaggageToWorkloadMetadata(absl::string_view data, absl::string_view identity) {
  absl::string_view instance;
  absl::string_view cluster;
  absl::string_view workload;
  absl::string_view namespace_name;
  absl::string_view canonical_name;
  absl::string_view canonical_revision;
  absl::string_view app_name;
  absl::string_view app_version;
  absl::string_view region;
  absl::string_view zone;
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
        // canonical name and app name are always the same
        canonical_name = parts.second;
        if (app_name.empty()) {
          app_name = parts.second;
        }
        break;
      case BaggageToken::ServiceVersion:
        // canonical revision and app version are always the same
        canonical_revision = parts.second;
        if (app_version.empty()) {
          app_version = parts.second;
        }
        break;
      case BaggageToken::AppName:
        app_name = parts.second;
        if (canonical_name.empty()) {
          canonical_name = parts.second;
        }
        break;
      case BaggageToken::AppVersion:
        app_version = parts.second;
        if (canonical_revision.empty()) {
          canonical_revision = parts.second;
        }
        break;
      case BaggageToken::WorkloadName: {
        workload = parts.second;
        std::vector<absl::string_view> splitWorkloadKey = absl::StrSplit(parts.first, ".");
        if (splitWorkloadKey.size() >= 2 && splitWorkloadKey[0] == "k8s") {
          workload_type = fromSuffix(splitWorkloadKey[1]);
        }
        break;
      }
      case BaggageToken::InstanceName:
        instance = parts.second;
        break;
      case BaggageToken::LocalityRegion:
        region = parts.second;
        break;
      case BaggageToken::LocalityZone:
        zone = parts.second;
        break;
      default:
        break;
      }
    }
  }
  return std::make_unique<WorkloadMetadataObject>(
      instance, cluster, namespace_name, workload, canonical_name, canonical_revision, app_name,
      app_version, workload_type, identity, region, zone);
}

} // namespace Common
} // namespace Istio
} // namespace Envoy
