#include "source/server/admin/config_dump_handler.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/common/regex.h"
#include "source/common/common/statusor.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

namespace {

// Validates that `field_mask` is valid for `message` and applies `TrimMessage`.
// Necessary because TrimMessage crashes if `field_mask` is invalid.
// Returns `true` on success.
bool checkFieldMaskAndTrimMessage(const Protobuf::FieldMask& field_mask,
                                  Protobuf::Message& message) {
  for (const auto& path : field_mask.paths()) {
    if (!ProtobufUtil::FieldMaskUtil::GetFieldDescriptors(message.GetDescriptor(), path, nullptr)) {
      return false;
    }
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
      ProtobufWkt::Any any_message;
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
      MessageUtil::unpackTo(any_message, *inner_message);
      // Trim message.
      if (!checkFieldMaskAndTrimMessage(inner_field_mask, *inner_message)) {
        return false;
      }
      // Pack it back into the Any resource.
      any_message.PackFrom(*inner_message);
      reflection->MutableMessage(&message, any_field)->CopyFrom(any_message);
    }
  }
  return checkFieldMaskAndTrimMessage(outer_field_mask, message);
}

// Helper method to get the eds parameter.
bool shouldIncludeEdsInDump(const Http::Utility::QueryParamsMulti& params) {
  return params.getFirstValue("include_eds").has_value();
}

absl::StatusOr<Matchers::StringMatcherPtr>
buildNameMatcher(const Http::Utility::QueryParamsMulti& params) {
  const auto name_regex = params.getFirstValue("name_regex");
  if (!name_regex.has_value() || name_regex->empty()) {
    return std::make_unique<Matchers::UniversalStringMatcher>();
  }
  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_google_re2() = envoy::type::matcher::v3::RegexMatcher::GoogleRE2();
  matcher.set_regex(*name_regex);
  TRY_ASSERT_MAIN_THREAD
  return Regex::Utility::parseRegex(matcher);
  END_TRY
  catch (EnvoyException& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("Error while parsing name_regex from ", *name_regex, ": ", e.what()));
  }
}

} // namespace

ConfigDumpHandler::ConfigDumpHandler(ConfigTracker& config_tracker, Server::Instance& server)
    : HandlerContextBase(server), config_tracker_(config_tracker) {}

Http::Code ConfigDumpHandler::handlerConfigDump(Http::ResponseHeaderMap& response_headers,
                                                Buffer::Instance& response,
                                                AdminStream& admin_stream) const {
  Http::Utility::QueryParamsMulti query_params = admin_stream.queryParams();
  const auto resource = query_params.getFirstValue("resource");
  const auto mask = query_params.getFirstValue("mask");
  const bool include_eds = shouldIncludeEdsInDump(query_params);
  const absl::StatusOr<Matchers::StringMatcherPtr> name_matcher = buildNameMatcher(query_params);
  if (!name_matcher.ok()) {
    response.add(name_matcher.status().ToString());
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
    return Http::Code::BadRequest;
  }

  envoy::admin::v3::ConfigDump dump;

  absl::optional<std::pair<Http::Code, std::string>> err;
  if (resource.has_value()) {
    err = addResourceToDump(dump, mask, resource.value(), **name_matcher, include_eds);
  } else {
    err = addAllConfigToDump(dump, mask, **name_matcher, include_eds);
  }
  if (err.has_value()) {
    response_headers.addReference(Http::Headers::get().XContentTypeOptions,
                                  Http::Headers::get().XContentTypeOptionValues.Nosniff);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
    response.add(err.value().second);
    return err.value().first;
  }
  MessageUtil::redact(dump);

  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  response.add(MessageUtil::getJsonStringFromMessageOrError(dump, true)); // pretty-print
  return Http::Code::OK;
}

absl::optional<std::pair<Http::Code, std::string>> ConfigDumpHandler::addResourceToDump(
    envoy::admin::v3::ConfigDump& dump, const absl::optional<std::string>& mask,
    const std::string& resource, const Matchers::StringMatcher& name_matcher,
    bool include_eds) const {
  Envoy::Server::ConfigTracker::CbsMap callbacks_map = config_tracker_.getCallbacksMap();
  if (include_eds) {
    // TODO(mattklein123): Add ability to see warming clusters in admin output.
    auto all_clusters = server_.clusterManager().clusters();
    if (!all_clusters.active_clusters_.empty()) {
      callbacks_map.emplace("endpoint", [this](const Matchers::StringMatcher& name_matcher) {
        return dumpEndpointConfigs(name_matcher);
      });
    }
  }

  for (const auto& [name, callback] : callbacks_map) {
    UNREFERENCED_PARAMETER(name);
    ProtobufTypes::MessagePtr message = callback(name_matcher);
    ASSERT(message);

    auto field_descriptor = message->GetDescriptor()->FindFieldByName(resource);
    const Protobuf::Reflection* reflection = message->GetReflection();
    if (!field_descriptor) {
      continue;
    } else if (!field_descriptor->is_repeated()) {
      return absl::optional<std::pair<Http::Code, std::string>>{std::make_pair(
          Http::Code::BadRequest,
          fmt::format("{} is not a repeated field. Use ?mask={} to get only this field",
                      field_descriptor->name(), field_descriptor->name()))};
    }

    auto repeated = reflection->GetRepeatedPtrField<Protobuf::Message>(*message, field_descriptor);
    for (Protobuf::Message& msg : repeated) {
      if (mask.has_value()) {
        Protobuf::FieldMask field_mask;
        ProtobufUtil::FieldMaskUtil::FromString(mask.value(), &field_mask);
        if (!trimResourceMessage(field_mask, msg)) {
          return absl::optional<std::pair<Http::Code, std::string>>{std::make_pair(
              Http::Code::BadRequest, absl::StrCat("FieldMask ", field_mask.DebugString(),
                                                   " could not be successfully used."))};
        }
      }
      auto* config = dump.add_configs();
      config->PackFrom(msg);
    }

    // We found the desired resource so there is no need to continue iterating over
    // the other keys.
    return absl::nullopt;
  }

  return absl::optional<std::pair<Http::Code, std::string>>{
      std::make_pair(Http::Code::NotFound, fmt::format("{} not found in config dump", resource))};
}

absl::optional<std::pair<Http::Code, std::string>> ConfigDumpHandler::addAllConfigToDump(
    envoy::admin::v3::ConfigDump& dump, const absl::optional<std::string>& mask,
    const Matchers::StringMatcher& name_matcher, bool include_eds) const {
  Envoy::Server::ConfigTracker::CbsMap callbacks_map = config_tracker_.getCallbacksMap();
  if (include_eds) {
    // TODO(mattklein123): Add ability to see warming clusters in admin output.
    auto all_clusters = server_.clusterManager().clusters();
    if (!all_clusters.active_clusters_.empty()) {
      callbacks_map.emplace("endpoint", [this](const Matchers::StringMatcher& name_matcher) {
        return dumpEndpointConfigs(name_matcher);
      });
    }
  }

  for (const auto& [name, callback] : callbacks_map) {
    UNREFERENCED_PARAMETER(name);
    ProtobufTypes::MessagePtr message = callback(name_matcher);
    ASSERT(message);

    if (mask.has_value()) {
      Protobuf::FieldMask field_mask;
      ProtobufUtil::FieldMaskUtil::FromString(mask.value(), &field_mask);
      // We don't use trimMessage() above here since masks don't support
      // indexing through repeated fields. We don't return error on failure
      // because different callback return types will have different valid
      // field masks.
      if (!checkFieldMaskAndTrimMessage(field_mask, *message)) {
        continue;
      }
    }

    auto* config = dump.add_configs();
    config->PackFrom(*message);
  }
  if (dump.configs().empty() && mask.has_value()) {
    return absl::optional<std::pair<Http::Code, std::string>>{std::make_pair(
        Http::Code::BadRequest,
        absl::StrCat("FieldMask ", *mask, " could not be successfully applied to any configs."))};
  }
  return absl::nullopt;
}

ProtobufTypes::MessagePtr
ConfigDumpHandler::dumpEndpointConfigs(const Matchers::StringMatcher& name_matcher) const {
  auto endpoint_config_dump = std::make_unique<envoy::admin::v3::EndpointsConfigDump>();
  // TODO(mattklein123): Add ability to see warming clusters in admin output.
  auto all_clusters = server_.clusterManager().clusters();
  for (const auto& [name, cluster_ref] : all_clusters.active_clusters_) {
    UNREFERENCED_PARAMETER(name);
    const Upstream::Cluster& cluster = cluster_ref.get();
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.info();
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;

    if (!cluster_info->edsServiceName().empty()) {
      cluster_load_assignment.set_cluster_name(cluster_info->edsServiceName());
    } else {
      cluster_load_assignment.set_cluster_name(cluster_info->name());
    }
    if (!name_matcher.match(cluster_load_assignment.cluster_name())) {
      continue;
    }
    auto& policy = *cluster_load_assignment.mutable_policy();

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
    if (cluster_info->addedViaApi()) {
      auto& dynamic_endpoint = *endpoint_config_dump->mutable_dynamic_endpoint_configs()->Add();
      dynamic_endpoint.mutable_endpoint_config()->PackFrom(cluster_load_assignment);
    } else {
      auto& static_endpoint = *endpoint_config_dump->mutable_static_endpoint_configs()->Add();
      static_endpoint.mutable_endpoint_config()->PackFrom(cluster_load_assignment);
    }
  }
  return endpoint_config_dump;
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
  auto& health_check_config = *endpoint.mutable_health_check_config();
  health_check_config.set_hostname(host->hostnameForHealthChecks());
  if (host->healthCheckAddress()->asString() != host->address()->asString()) {
    health_check_config.set_port_value(host->healthCheckAddress()->ip()->port());
  }
}

} // namespace Server
} // namespace Envoy
