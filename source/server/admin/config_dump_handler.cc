#include "source/server/admin/config_dump_handler.h"

#include <algorithm>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/common/regex.h"
#include "source/common/http/headers.h"
#include "source/common/json/proto_streamer.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

namespace {

constexpr absl::string_view EndpointsComponentName = "endpoint";

bool fieldMaskFits(const Protobuf::FieldMask& field_mask, const Protobuf::Descriptor& descriptor) {
  for (const auto& path : field_mask.paths()) {
    if (!ProtobufUtil::FieldMaskUtil::GetFieldDescriptors(&descriptor, path, nullptr)) {
      return false;
    }
  }
  return true;
}

// Validates that `field_mask` is valid for `message` and applies `TrimMessage`.
// Necessary because TrimMessage crashes if `field_mask` is invalid.
// Returns `true` on success.
bool checkFieldMaskAndTrimMessage(const Protobuf::FieldMask& field_mask,
                                  Protobuf::Message& message) {
  if (!fieldMaskFits(field_mask, *message.GetDescriptor())) {
    return false;
  }
  ProtobufUtil::FieldMaskUtil::TrimMessage(field_mask, &message);
  return true;
}

// Apply a field mask to a resource message. A simple field mask might look
// like "cluster.name,cluster.alt_stat_name,last_updated" for a StaticCluster
// resource. Unfortunately, since the "cluster" field is Any and the in-built
// FieldMask utils can't mask inside an Any field, we need to do additional work
// below.
//
// We take advantage of the fact that for the most part (with the exception of
// DynamicListener) that ConfigDump resources have a single Any field where the
// embedded resources lives. This allows us to construct an inner field mask for
// the Any resource and an outer field mask for the enclosing message. In the
// above example, the inner field mask would be "name,alt_stat_name" and the
// outer field mask "cluster,last_updated". The masks are applied to their
// respective messages, with the Any resource requiring an unpack/mask/pack
// series of operations.
//
// TODO(htuch): we could make field masks more powerful in future and generalize
// this to allow arbitrary indexing through Any fields. This is pretty
// complicated, we would need to build a FieldMask tree similar to how the C++
// Protobuf library does this internally.
/**
 * @return true on success, false if `field_mask` is invalid.
 */
bool trimResourceMessage(const Protobuf::FieldMask& field_mask, Protobuf::Message& message) {
  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  // Figure out which paths cover Any fields. For each field, gather the paths to
  // an inner mask, switch the outer mask to cover only the original field.
  Protobuf::FieldMask outer_field_mask;
  Protobuf::FieldMask inner_field_mask;
  std::string any_field_name;
  for (int i = 0; i < field_mask.paths().size(); ++i) {
    const std::string& path = field_mask.paths(i);
    std::vector<std::string> frags = absl::StrSplit(path, '.');
    if (frags.empty()) {
      continue;
    }
    const Protobuf::FieldDescriptor* field = descriptor->FindFieldByName(frags[0]);
    // Only a single Any field supported, repeated fields don't support further
    // indexing.
    // TODO(htuch): should add support for DynamicListener for multiple Any
    // fields in the future, see
    // https://github.com/envoyproxy/envoy/issues/9669.
    if (field != nullptr && field->message_type() != nullptr && !field->is_repeated() &&
        field->message_type()->full_name() == "google.protobuf.Any") {
      if (any_field_name.empty()) {
        any_field_name = frags[0];
      } else {
        // This should be structurally true due to the ConfigDump proto
        // definition (but not for DynamicListener today).
        ASSERT(any_field_name == frags[0],
               "Only a single Any field in a config dump resource is supported.");
      }
      outer_field_mask.add_paths(frags[0]);
      frags.erase(frags.begin());
      inner_field_mask.add_paths(absl::StrJoin(frags, "."));
    } else {
      outer_field_mask.add_paths(path);
    }
  }

  if (!any_field_name.empty()) {
    const Protobuf::FieldDescriptor* any_field = descriptor->FindFieldByName(any_field_name);
    if (reflection->HasField(message, any_field)) {
      ASSERT(any_field != nullptr);
      // Unpack to a DynamicMessage.
      Protobuf::Any any_message;
      any_message.MergeFrom(reflection->GetMessage(message, any_field));
      Protobuf::DynamicMessageFactory dmf;
      const absl::string_view inner_type_name =
          TypeUtil::typeUrlToDescriptorFullName(any_message.type_url());
      const Protobuf::Descriptor* inner_descriptor =
          Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
              static_cast<std::string>(inner_type_name));
      ASSERT(inner_descriptor != nullptr);
      std::unique_ptr<Protobuf::Message> inner_message;
      inner_message.reset(dmf.GetPrototype(inner_descriptor)->New());
      if (!MessageUtil::unpackTo(any_message, *inner_message).ok()) {
        return false;
      }
      // Trim message.
      if (!checkFieldMaskAndTrimMessage(inner_field_mask, *inner_message)) {
        return false;
      }
      // Pack it back into the Any resource.
      std::ignore = any_message.PackFrom(*inner_message);
      reflection->MutableMessage(&message, any_field)->CopyFrom(any_message);
    }
  }
  return checkFieldMaskAndTrimMessage(outer_field_mask, message);
}

// Helper method to get the eds parameter.
bool shouldIncludeEdsInDump(const Http::Utility::QueryParamsMulti& params) {
  return params.getFirstValue("include_eds").has_value();
}

// The cluster's name, or its eds service name if it has one
const std::string& dumpedClusterName(const Upstream::ClusterInfo& cluster_info) {
  return cluster_info.edsServiceName().empty() ? cluster_info.name()
                                               : cluster_info.edsServiceName();
}

absl::StatusOr<Matchers::StringMatcherPtr>
buildNameMatcher(const Http::Utility::QueryParamsMulti& params, Regex::Engine& engine) {
  const auto name_regex = params.getFirstValue("name_regex");
  if (!name_regex.has_value() || name_regex->empty()) {
    return std::make_unique<Matchers::UniversalStringMatcher>();
  }
  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_google_re2() = envoy::type::matcher::v3::RegexMatcher::GoogleRE2();
  matcher.set_regex(*name_regex);
  auto regex_or_error = Regex::Utility::parseRegex(matcher, engine);
  if (regex_or_error.status().ok()) {
    return std::move(*regex_or_error);
  }
  return absl::InvalidArgumentError(absl::StrCat("Error while parsing name_regex from ",
                                                 *name_regex, ": ",
                                                 regex_or_error.status().message()));
}

} // namespace

ConfigDumpHandler::ConfigDumpHandler(ConfigTracker& config_tracker, Server::Instance& server)
    : HandlerContextBase(server), config_tracker_(config_tracker) {}

Admin::UrlHandler ConfigDumpHandler::handlerConfigDumpStreamed() {
  return {"/config_dump",
          "dump current Envoy configs",
          [this](AdminStream& admin_stream) { return makeRequest(admin_stream); },
          false,
          false,
          {{Admin::ParamDescriptor::Type::String, "resource", "The resource to dump"},
           {Admin::ParamDescriptor::Type::String, "mask",
            "The mask to apply. When both resource and mask are specified, "
            "the mask is applied to every element in the desired repeated field so that only a "
            "subset of fields are returned. The mask is parsed as a Protobuf::FieldMask"},
           {Admin::ParamDescriptor::Type::String, "name_regex",
            "Dump only the currently loaded configurations whose names match the specified "
            "regex. Can be used with both resource and mask query parameters."},
           {Admin::ParamDescriptor::Type::Boolean, "include_eds",
            "Dump currently loaded configuration including EDS. See the response definition "
            "for more information"}}};
}

Admin::RequestPtr ConfigDumpHandler::makeRequest(AdminStream& admin_stream) const {
  Http::Utility::QueryParamsMulti query_params = admin_stream.queryParams();
  const std::optional<std::string> resource = Utility::nonEmptyQueryParam(query_params, "resource");
  const std::optional<std::string> mask = Utility::nonEmptyQueryParam(query_params, "mask");

  absl::StatusOr<Matchers::StringMatcherPtr> name_matcher =
      buildNameMatcher(query_params, server_.regexEngine());
  if (!name_matcher.ok()) {
    return Admin::makeStaticTextRequest(name_matcher.status().ToString(), Http::Code::BadRequest);
  }

  const bool include_eds = shouldIncludeEdsInDump(query_params);

  ConfigDumpRequest::Dump dump{
      std::move(name_matcher.value()),
      componentNames(include_eds),
      include_eds,
  };

  if (resource.has_value()) {
    absl::StatusOr<ResourceField> resource_field = findResource(
        resource.value(), mask, dump.component_names_, dump.include_eds_, *dump.name_matcher_);
    if (!resource_field.ok()) {
      return Admin::makeStaticTextRequest(
          resource_field.status().message(),
          resource_field.status().code() == absl::StatusCode::kNotFound ? Http::Code::NotFound
                                                                        : Http::Code::BadRequest);
    }
    dump.resource_ = std::move(resource_field.value());
  } else if (mask.has_value()) {
    ProtobufUtil::FieldMaskUtil::FromString(mask.value(), &dump.field_mask_.emplace());
    dump.pending_config_ = firstMaskedConfig(*dump.field_mask_, dump.component_names_,
                                             dump.include_eds_, *dump.name_matcher_);
    if (dump.pending_config_ == nullptr) {
      return Admin::makeStaticTextRequest(
          absl::StrCat("FieldMask ", mask.value(),
                       " could not be successfully applied to any configs."),
          Http::Code::BadRequest);
    }
  }

  return std::make_unique<ConfigDumpRequest>(*this, std::move(dump));
}

std::deque<std::string> ConfigDumpHandler::componentNames(bool include_eds) const {
  std::deque<std::string> names;
  for (const auto& callback : config_tracker_.getCallbacksMap()) {
    names.push_back(callback.first);
  }
  // TODO(mattklein123): Add ability to see warming clusters in admin output.
  if (include_eds && server_.clusterManager().hasActiveClusters()) {
    const std::string endpoints_name(EndpointsComponentName);
    const auto at = std::lower_bound(names.begin(), names.end(), endpoints_name);
    if (at == names.end() || *at != endpoints_name) {
      names.insert(at, endpoints_name);
    }
  }
  return names;
}

bool ConfigDumpHandler::dumpsEndpoints(const std::string& name, bool include_eds) const {
  return include_eds && name == EndpointsComponentName &&
         !config_tracker_.getCallbacksMap().contains(name);
}

std::optional<envoy::config::endpoint::v3::ClusterLoadAssignment>
ConfigDumpHandler::buildClusterLoadAssignment(const Upstream::Cluster& cluster,
                                              const Matchers::StringMatcher& name_matcher) const {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.info();

  cluster_load_assignment.set_cluster_name(dumpedClusterName(*cluster_info));
  if (!name_matcher.match(cluster_load_assignment.cluster_name())) {
    return std::nullopt;
  }
  auto& policy = *cluster_load_assignment.mutable_policy();

  // Using MILLION as denominator in config dump.
  float value = cluster.dropOverload().value() * 1000000;
  if (value > 0) {
    auto* drop_overload = policy.add_drop_overloads();
    drop_overload->set_category(cluster.dropCategory());
    auto* percent = drop_overload->mutable_drop_percentage();
    percent->set_denominator(envoy::type::v3::FractionalPercent::MILLION);
    percent->set_numerator(uint32_t(value));
  }

  for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
    policy.mutable_overprovisioning_factor()->set_value(host_set->overprovisioningFactor());

    if (!host_set->hostsPerLocality().get().empty()) {
      for (int index = 0; index < static_cast<int>(host_set->hostsPerLocality().get().size());
           index++) {
        auto locality_host_set = host_set->hostsPerLocality().get()[index];

        if (!locality_host_set.empty()) {
          auto& locality_lb_endpoint = *cluster_load_assignment.mutable_endpoints()->Add();
          locality_lb_endpoint.mutable_locality()->MergeFrom(locality_host_set[0]->locality());
          locality_lb_endpoint.set_priority(locality_host_set[0]->priority());
          if (host_set->localityWeights() != nullptr && !host_set->localityWeights()->empty()) {
            locality_lb_endpoint.mutable_load_balancing_weight()->set_value(
                (*host_set->localityWeights())[index]);
          }

          for (auto& host : locality_host_set) {
            addLbEndpoint(host, locality_lb_endpoint);
          }
        }
      }
    } else {
      for (auto& host : host_set->hosts()) {
        auto& locality_lb_endpoint = *cluster_load_assignment.mutable_endpoints()->Add();
        locality_lb_endpoint.mutable_locality()->MergeFrom(host->locality());
        locality_lb_endpoint.set_priority(host->priority());
        addLbEndpoint(host, locality_lb_endpoint);
      }
    }
  }
  return cluster_load_assignment;
}

ProtobufTypes::MessagePtr
ConfigDumpHandler::dumpEndpointConfigs(const Matchers::StringMatcher& name_matcher) const {
  auto endpoint_config_dump = std::make_unique<envoy::admin::v3::EndpointsConfigDump>();
  // TODO(mattklein123): Add ability to see warming clusters in admin output.
  server_.clusterManager().forEachActiveCluster([&](const Upstream::Cluster& cluster) {
    std::optional<envoy::config::endpoint::v3::ClusterLoadAssignment> cluster_load_assignment =
        buildClusterLoadAssignment(cluster, name_matcher);
    if (!cluster_load_assignment) {
      return;
    }

    if (cluster.info()->addedViaApi()) {
      auto& dynamic_endpoint = *endpoint_config_dump->mutable_dynamic_endpoint_configs()->Add();
      std::ignore = dynamic_endpoint.mutable_endpoint_config()->PackFrom(*cluster_load_assignment);
    } else {
      auto& static_endpoint = *endpoint_config_dump->mutable_static_endpoint_configs()->Add();
      std::ignore = static_endpoint.mutable_endpoint_config()->PackFrom(*cluster_load_assignment);
    }
  });
  return endpoint_config_dump;
}

absl::StatusOr<ResourceField>
ConfigDumpHandler::findResource(const std::string& resource, const std::optional<std::string>& mask,
                                std::deque<std::string>& component_names, bool include_eds,
                                const Matchers::StringMatcher& name_matcher) const {
  Protobuf::FieldMask field_mask;
  if (mask.has_value()) {
    ProtobufUtil::FieldMaskUtil::FromString(mask.value(), &field_mask);
  }
  // Dumping the endpoints walks every cluster, so check for the field before paying for it.
  const bool endpoints_can_hold_resource =
      envoy::admin::v3::EndpointsConfigDump::descriptor()->FindFieldByName(resource) != nullptr;

  while (!component_names.empty()) {
    const std::string name = std::move(component_names.front());
    component_names.pop_front();
    // Skipped without dumping, since EndpointsConfigDump has no field by that name.
    if (!endpoints_can_hold_resource && dumpsEndpoints(name, include_eds)) {
      continue;
    }

    ProtobufTypes::MessagePtr message = dumpComponent(name, include_eds, name_matcher);
    if (message == nullptr) {
      continue;
    }

    const Protobuf::FieldDescriptor* field = message->GetDescriptor()->FindFieldByName(resource);
    if (field == nullptr) {
      continue;
    }
    if (!field->is_repeated()) {
      return absl::InvalidArgumentError(
          fmt::format("{} is not a repeated field. Use ?mask={} to get only this field",
                      field->name(), field->name()));
    }
    if (field->cpp_type() != Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      return absl::InvalidArgumentError(
          fmt::format("{} is not a repeated message field", field->name()));
    }

    if (mask.has_value()) {
      // Whether the mask applies depends on the element, so every element is trimmed now, while a
      // failure can still be the response code.
      const Protobuf::Reflection* reflection = message->GetReflection();
      const int size = reflection->FieldSize(*message, field);
      for (int i = 0; i < size; ++i) {
        if (!trimResourceMessage(field_mask,
                                 *reflection->MutableRepeatedMessage(message.get(), field, i))) {
          return absl::InvalidArgumentError(absl::StrCat("FieldMask ", field_mask.DebugString(),
                                                         " could not be successfully used."));
        }
      }
    }

    return ResourceField{std::move(message), field};
  }

  return absl::NotFoundError(fmt::format("{} not found in config dump", resource));
}

ProtobufTypes::MessagePtr
ConfigDumpHandler::firstMaskedConfig(const Protobuf::FieldMask& field_mask,
                                     std::deque<std::string>& component_names, bool include_eds,
                                     const Matchers::StringMatcher& name_matcher) const {
  // Dumping the endpoints walks every cluster, so check up front whether the mask fits
  // EndpointsConfigDump at all.
  const bool endpoints_fit_mask =
      fieldMaskFits(field_mask, *envoy::admin::v3::EndpointsConfigDump::descriptor());

  while (!component_names.empty()) {
    const std::string name = std::move(component_names.front());
    component_names.pop_front();
    if (!endpoints_fit_mask && dumpsEndpoints(name, include_eds)) {
      continue;
    }

    ProtobufTypes::MessagePtr message = dumpComponent(name, include_eds, name_matcher);
    if (message != nullptr && checkFieldMaskAndTrimMessage(field_mask, *message)) {
      return message;
    }
  }
  return nullptr;
}

std::deque<EndpointCluster>
ConfigDumpHandler::snapshotEndpointClusters(const Matchers::StringMatcher& name_matcher) const {
  std::deque<EndpointCluster> clusters;
  // TODO(mattklein123): Add ability to see warming clusters in admin output.
  server_.clusterManager().forEachActiveCluster([&](const Upstream::Cluster& cluster) {
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.info();
    if (!name_matcher.match(dumpedClusterName(*cluster_info))) {
      return;
    }
    clusters.push_back({cluster_info->name(), cluster_info->addedViaApi()});
  });
  std::stable_partition(clusters.begin(), clusters.end(),
                        [](const EndpointCluster& cluster) { return !cluster.dynamic_; });
  return clusters;
}

std::unique_ptr<envoy::config::endpoint::v3::ClusterLoadAssignment>
ConfigDumpHandler::dumpClusterLoadAssignment(const EndpointCluster& endpoint_cluster,
                                             const Matchers::StringMatcher& name_matcher) const {
  const OptRef<const Upstream::Cluster> cluster =
      server_.clusterManager().getActiveCluster(endpoint_cluster.name_);
  if (!cluster.has_value()) {
    return nullptr;
  }
  std::optional<envoy::config::endpoint::v3::ClusterLoadAssignment> cluster_load_assignment =
      buildClusterLoadAssignment(*cluster, name_matcher);
  if (!cluster_load_assignment.has_value()) {
    return nullptr;
  }
  return std::make_unique<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      std::move(*cluster_load_assignment));
}

ProtobufTypes::MessagePtr
ConfigDumpHandler::trackedConfig(const std::string& name,
                                 const Matchers::StringMatcher& name_matcher) const {
  const ConfigTracker::CbsMap& callbacks = config_tracker_.getCallbacksMap();
  // The find can miss because the entry was destroyed after snapshot.
  const auto callback = callbacks.find(name);
  if (callback == callbacks.end()) {
    return nullptr;
  }
  ProtobufTypes::MessagePtr message = callback->second(name_matcher);
  ASSERT(message != nullptr);
  return message;
}

ProtobufTypes::MessagePtr
ConfigDumpHandler::dumpComponent(const std::string& name, bool include_eds,
                                 const Matchers::StringMatcher& name_matcher) const {
  if (dumpsEndpoints(name, include_eds)) {
    return dumpEndpointConfigs(name_matcher);
  }
  return trackedConfig(name, name_matcher);
}

void ConfigDumpHandler::addLbEndpoint(
    const Upstream::HostSharedPtr& host,
    envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) const {
  auto& lb_endpoint = *locality_lb_endpoint.mutable_lb_endpoints()->Add();
  if (host->metadata() != nullptr) {
    lb_endpoint.mutable_metadata()->MergeFrom(*host->metadata());
  }
  lb_endpoint.mutable_load_balancing_weight()->set_value(host->weight());

  switch (host->coarseHealth()) {
  case Upstream::Host::Health::Healthy:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::HEALTHY);
    break;
  case Upstream::Host::Health::Unhealthy:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::UNHEALTHY);
    break;
  case Upstream::Host::Health::Degraded:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::DEGRADED);
    break;
  default:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::UNKNOWN);
  }

  auto& endpoint = *lb_endpoint.mutable_endpoint();
  endpoint.set_hostname(host->hostname());
  Network::Utility::addressToProtobufAddress(*host->address(), *endpoint.mutable_address());
  if (host->addressListOrNull() != nullptr) {
    const auto& address_list = *host->addressListOrNull();
    if (address_list.size() > 1) {
      // skip first address of the list as the default address is not an additional one.
      for (auto it = std::next(address_list.begin()); it != address_list.end(); ++it) {
        auto& new_address = *endpoint.mutable_additional_addresses()->Add();
        Network::Utility::addressToProtobufAddress(**it, *new_address.mutable_address());
      }
    }
  }
  auto& health_check_config = *endpoint.mutable_health_check_config();
  health_check_config.set_hostname(host->hostnameForHealthChecks());
  if (host->healthCheckAddress()->asString() != host->address()->asString()) {
    health_check_config.set_port_value(host->healthCheckAddress()->ip()->port());
  }
}

class ConfigDumpRequest::Document {
public:
  explicit Document(Buffer::Instance& response)
      : streamer_(response), root_map_(streamer_.makeRootMap()) {
    root_map_->addKey("configs");
    configs_array_ = root_map_->addArray();
  }

  // Whether a config is only partly emitted.
  bool streaming() const { return config_ != nullptr; }

  /**
   * Emits the next piece of the config.
   * @return false if no config was being walked.
   */
  bool advance() {
    if (config_ == nullptr) {
      return false;
    }
    if (!config_->streamer_->next()) {
      config_.reset();
    }
    return true;
  }

  // Starts emitting `config` as an entry of `configs`.
  void streamConfig(const Protobuf::Message& config) { streamInto(config, *configs_array_); }

  // Starts emitting `config` as an entry of `configs`.
  void streamConfig(ProtobufTypes::MessagePtr config) {
    streamInto(*config, *configs_array_);
    config_->owned_ = std::move(config);
  }

  // Whether an entry is still being filled in.
  bool entryOpen() const { return entry_map_ != nullptr; }

  // Opens an entry of `configs` naming `type_url`.
  void openEntry(absl::string_view type_url) {
    ASSERT(!entryOpen());
    entry_map_ = configs_array_->addMap();
    entry_map_->addKey("@type");
    entry_map_->addString(type_url);
  }

  // Starts emitting `config` under `field` of a new element in the array named `array`. Elements of
  // one array must be added consecutively.
  void streamInEntry(absl::string_view array, absl::string_view field,
                     ProtobufTypes::MessagePtr config) {
    ASSERT(entryOpen());
    if (entry_array_key_ != array) {
      // Release the previous array first, or the new one opens inside it.
      entry_array_.reset();
      entry_map_->addKey(array);
      entry_array_ = entry_map_->addArray();
      entry_array_key_ = array;
    }
    Json::BufferStreamer::MapPtr element = entry_array_->addMap();
    element->addKey(field);
    streamInto(*config, *element);
    config_->owned_ = std::move(config);
    config_->element_ = std::move(element);
  }

  void closeEntry() {
    ASSERT(entryOpen() && !streaming());
    entry_array_.reset();
    entry_map_.reset();
    entry_array_key_.clear();
  }

private:
  // A config being emitted, and what has to stay alive until it is whole.
  struct StreamedConfig {
    ProtobufTypes::MessagePtr owned_;
    Json::BufferStreamer::MapPtr element_;
    std::unique_ptr<Json::MessageStreamer> streamer_;
  };

  void streamInto(const Protobuf::Message& config, Json::BufferStreamer::Level& level) {
    ASSERT(!streaming());
    config_ = std::make_unique<StreamedConfig>();
    config_->streamer_ = std::make_unique<Json::MessageStreamer>(
        config, level, Json::MessageStreamer::TypeUrl::Emit,
        Json::MessageStreamer::FieldNames::Proto, Json::MessageStreamer::Sensitive::Redact);
  }

  Json::BufferStreamer streamer_;
  Json::BufferStreamer::MapPtr root_map_;
  Json::BufferStreamer::ArrayPtr configs_array_;
  Json::BufferStreamer::MapPtr entry_map_;
  Json::BufferStreamer::ArrayPtr entry_array_;
  std::string entry_array_key_;
  std::unique_ptr<StreamedConfig> config_;
};

ConfigDumpRequest::ConfigDumpRequest(const ConfigDumpHandler& handler, Dump dump)
    : handler_(handler), dump_(std::move(dump)) {}

ConfigDumpRequest::~ConfigDumpRequest() = default;

Http::Code ConfigDumpRequest::start(Http::ResponseHeaderMap& response_headers) {
  document_ = std::make_unique<Document>(response_);
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  return Http::Code::OK;
}

bool ConfigDumpRequest::nextChunk(Buffer::Instance& response) {
  bool more = true;
  while (more && response_.length() < chunk_size_) {
    more = serializeNextConfig();
  }
  if (!more) {
    ASSERT(!document_->streaming() && !document_->entryOpen());
    document_.reset();
  }
  response.move(response_);

  return more;
}

bool ConfigDumpRequest::serializeNextResourceElement() {
  const ProtobufTypes::MessagePtr& resource = dump_.resource_->config_;
  const Protobuf::FieldDescriptor* field = dump_.resource_->field_;

  const Protobuf::Reflection* reflection = resource->GetReflection();
  if (next_resource_element_ >= reflection->FieldSize(*resource, field)) {
    return false;
  }
  document_->streamConfig(
      reflection->GetRepeatedMessage(*resource, field, next_resource_element_++));
  return true;
}

bool ConfigDumpRequest::serializeNextConfig() {
  if (document_->advance()) {
    return true;
  }

  if (document_->entryOpen()) {
    serializeNextEndpoint();
    return true;
  }

  if (dump_.resource_.has_value()) {
    return serializeNextResourceElement();
  }

  if (dump_.field_mask_.has_value()) {
    ProtobufTypes::MessagePtr message = nextMaskedConfig();
    if (message == nullptr) {
      return false;
    }
    document_->streamConfig(std::move(message));
    return true;
  }

  while (!dump_.component_names_.empty()) {
    const std::string name = std::move(dump_.component_names_.front());
    dump_.component_names_.pop_front();

    // The endpoints component is emitted a cluster at a time, so it only opens its entry here.
    if (handler_.dumpsEndpoints(name, dump_.include_eds_)) {
      endpoint_clusters_ = handler_.snapshotEndpointClusters(*dump_.name_matcher_);
      document_->openEntry(TypeUtil::descriptorFullNameToTypeUrl(
          envoy::admin::v3::EndpointsConfigDump::descriptor()->full_name()));
      return true;
    }

    ProtobufTypes::MessagePtr message = handler_.trackedConfig(name, *dump_.name_matcher_);
    // The component can be gone by now, in which case the next name is tried.
    if (message != nullptr) {
      document_->streamConfig(std::move(message));
      return true;
    }
  }

  return false;
}

ProtobufTypes::MessagePtr ConfigDumpRequest::nextMaskedConfig() {
  // The config makeRequest had to dump to decide the response code is emitted before the rest.
  if (dump_.pending_config_ != nullptr) {
    return std::move(dump_.pending_config_);
  }
  return handler_.firstMaskedConfig(*dump_.field_mask_, dump_.component_names_, dump_.include_eds_,
                                    *dump_.name_matcher_);
}

void ConfigDumpRequest::serializeNextEndpoint() {
  ASSERT(document_->entryOpen());
  while (!endpoint_clusters_.empty()) {
    const EndpointCluster cluster = std::move(endpoint_clusters_.front());
    endpoint_clusters_.pop_front();

    ProtobufTypes::MessagePtr config =
        handler_.dumpClusterLoadAssignment(cluster, *dump_.name_matcher_);
    // The cluster can be gone by now
    if (config == nullptr) {
      continue;
    }
    document_->streamInEntry(cluster.dynamic_ ? "dynamic_endpoint_configs"
                                              : "static_endpoint_configs",
                             "endpoint_config", std::move(config));
    return;
  }
  document_->closeEntry();
}

} // namespace Server
} // namespace Envoy
