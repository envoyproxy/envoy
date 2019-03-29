#include "common/config/grpc_mux_impl.h"

#include <unordered_set>

#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

std::unordered_map<std::string, std::unordered_set<envoy::admin::v2alpha::ControlPlaneConfigDump::ConfigSourceControlPlaneInfo,GrpcMuxImpl::ConfigSourceHash>> GrpcMuxImpl::service_control_plane_config_dump_;

GrpcMuxImpl::GrpcMuxImpl(const LocalInfo::LocalInfo& local_info, Grpc::AsyncClientPtr async_client,
                         Event::Dispatcher& dispatcher,
                         const Protobuf::MethodDescriptor& service_method,
                         Runtime::RandomGenerator& random, Stats::Scope& scope,
                         const RateLimitSettings& rate_limit_settings,
                         Server::ConfigTracker& config_tracker, const envoy::api::v2::core::GrpcService& grpc_service )
    : GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse, std::string>(
          std::move(async_client), service_method, random, dispatcher, scope, rate_limit_settings),
      local_info_(local_info), service_name_(service_method.service()->full_name()),
      config_tracker_entry_(config_tracker.add(service_method.service()->name(),
                                               [this] { return dumpControlPlaneConfig(); })),
      time_source_(dispatcher.timeSource()), grpc_service_(grpc_service) {
  Config::Utility::checkLocalInfo("ads", local_info);
}

GrpcMuxImpl::~GrpcMuxImpl() {
  for (const auto& api_state : api_state_) {
    for (auto watch : api_state.second.watches_) {
      watch->inserted_ = false;
    }
  }
}

void GrpcMuxImpl::start() { establishNewStream(); }

void GrpcMuxImpl::sendDiscoveryRequest(const std::string& type_url) {
  if (!grpcStreamAvailable()) {
    ENVOY_LOG(debug, "No stream available to sendDiscoveryRequest for {}", type_url);
    return; // Drop this request; the reconnect will enqueue a new one.
  }

  ApiState& api_state = api_state_[type_url];
  if (api_state.paused_) {
    ENVOY_LOG(trace, "API {} paused during sendDiscoveryRequest(), setting pending.", type_url);
    api_state.pending_ = true;
    return; // Drop this request; the unpause will enqueue a new one.
  }

  auto& request = api_state.request_;
  request.mutable_resource_names()->Clear();

  // Maintain a set to avoid dupes.
  std::unordered_set<std::string> resources;
  for (const auto* watch : api_state.watches_) {
    for (const std::string& resource : watch->resources_) {
      if (resources.count(resource) == 0) {
        resources.emplace(resource);
        request.add_resource_names(resource);
      }
    }
  }

  ENVOY_LOG(trace, "Sending DiscoveryRequest for {}: {}", type_url, request.DebugString());
  sendMessage(request);

  // clear error_detail after the request is sent if it exists.
  if (api_state_[type_url].request_.has_error_detail()) {
    api_state_[type_url].request_.clear_error_detail();
  }
}

GrpcMuxWatchPtr GrpcMuxImpl::subscribe(const std::string& type_url,
                                       const std::vector<std::string>& resources,
                                       GrpcMuxCallbacks& callbacks) {
  auto watch =
      std::unique_ptr<GrpcMuxWatch>(new GrpcMuxWatchImpl(resources, callbacks, type_url, *this));
  ENVOY_LOG(debug, "gRPC mux subscribe for " + type_url);

  // Lazily kick off the requests based on first subscription. This has the
  // convenient side-effect that we order messages on the channel based on
  // Envoy's internal dependency ordering.
  // TODO(gsagula): move TokenBucketImpl params to a config.
  if (!api_state_[type_url].subscribed_) {
    api_state_[type_url].request_.set_type_url(type_url);
    api_state_[type_url].request_.mutable_node()->MergeFrom(local_info_.node());
    api_state_[type_url].subscribed_ = true;
    subscriptions_.emplace_back(type_url);
  }

  // This will send an updated request on each subscription.
  // TODO(htuch): For RDS/EDS, this will generate a new DiscoveryRequest on each resource we added.
  // Consider in the future adding some kind of collation/batching during CDS/LDS updates so that we
  // only send a single RDS/EDS update after the CDS/LDS update.
  queueDiscoveryRequest(type_url);

  return watch;
}

void GrpcMuxImpl::pause(const std::string& type_url) {
  ENVOY_LOG(debug, "Pausing discovery requests for {}", type_url);
  ApiState& api_state = api_state_[type_url];
  ASSERT(!api_state.paused_);
  ASSERT(!api_state.pending_);
  api_state.paused_ = true;
}

void GrpcMuxImpl::resume(const std::string& type_url) {
  ENVOY_LOG(debug, "Resuming discovery requests for {}", type_url);
  ApiState& api_state = api_state_[type_url];
  ASSERT(api_state.paused_);
  api_state.paused_ = false;

  if (api_state.pending_) {
    ASSERT(api_state.subscribed_);
    queueDiscoveryRequest(type_url);
    api_state.pending_ = false;
  }
}

void GrpcMuxImpl::handleResponse(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) {
  const std::string& type_url = message->type_url();
  ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url, message->version_info());
  if (api_state_.count(type_url) == 0) {
    ENVOY_LOG(warn, "Ignoring the message for type URL {} as it has no current subscribers.",
              type_url);
    // TODO(yuval-k): This should never happen. consider dropping the stream as this is a protocol
    // violation
    return;
  }

//   if (message->has_control_plane()) {
//   if (service_control_plane_config_dump_.count(service_name_) == 0) {
// service_control_plane_config_dump_.insert({service_name_, });
//   }

   envoy::admin::v2alpha::ControlPlaneConfigDump::ConfigSourceControlPlaneInfo config_source_info;
      config_source_info.mutable_control_plane()->set_identifier("control_plane_1");
       TimestampUtil::systemClockToTimestamp(time_source_.systemTime(),
                                         *(config_source_info.mutable_last_updated()));
  GrpcMuxImpl::service_control_plane_config_dump_[service_name_].insert(config_source_info);  
  

  // }

  if (api_state_[type_url].watches_.empty()) {
    // update the nonce as we are processing this response.
    api_state_[type_url].request_.set_response_nonce(message->nonce());
    if (message->resources().empty()) {
      // No watches and no resources. This can happen when envoy unregisters from a resource
      // that's removed from the server as well. For example, a deleted cluster triggers un-watching
      // the ClusterLoadAssignment watch, and at the same time the xDS server sends an empty list of
      // ClusterLoadAssignment resources. we'll accept this update. no need to send a discovery
      // request, as we don't watch for anything.
      api_state_[type_url].request_.set_version_info(message->version_info());
    } else {
      // No watches and we have resources - this should not happen. send a NACK (by not updating
      // the version).
      ENVOY_LOG(warn, "Ignoring unwatched type URL {}", type_url);
      queueDiscoveryRequest(type_url);
    }
    return;
  }
  try {
    // To avoid O(n^2) explosion (e.g. when we have 1000s of EDS watches), we
    // build a map here from resource name to resource and then walk watches_.
    // We have to walk all watches (and need an efficient map as a result) to
    // ensure we deliver empty config updates when a resource is dropped.
    std::unordered_map<std::string, ProtobufWkt::Any> resources;
    GrpcMuxCallbacks& callbacks = api_state_[type_url].watches_.front()->callbacks_;
    for (const auto& resource : message->resources()) {
      if (type_url != resource.type_url()) {
        throw EnvoyException(fmt::format("{} does not match {} type URL in DiscoveryResponse {}",
                                         resource.type_url(), type_url, message->DebugString()));
      }
      const std::string resource_name = callbacks.resourceName(resource);
      resources.emplace(resource_name, resource);
    }
    for (auto watch : api_state_[type_url].watches_) {
      // onConfigUpdate should be called in all cases for single watch xDS (Cluster and Listener)
      // even if the message does not have resources so that update_empty stat is properly
      // incremented and state-of-the-world semantics are maintained.
      if (watch->resources_.empty()) {
        watch->callbacks_.onConfigUpdate(message->resources(), message->version_info());
        continue;
      }
      Protobuf::RepeatedPtrField<ProtobufWkt::Any> found_resources;
      for (auto watched_resource_name : watch->resources_) {
        auto it = resources.find(watched_resource_name);
        if (it != resources.end()) {
          found_resources.Add()->MergeFrom(it->second);
        }
      }
      // onConfigUpdate should be called only on watches(clusters/routes) that have updates in the
      // message for EDS/RDS.
      if (found_resources.size() > 0) {
        watch->callbacks_.onConfigUpdate(found_resources, message->version_info());
      }
    }
    // TODO(mattklein123): In the future if we start tracking per-resource versions, we would do
    // that tracking here.
    api_state_[type_url].request_.set_version_info(message->version_info());
  } catch (const EnvoyException& e) {
    for (auto watch : api_state_[type_url].watches_) {
      watch->callbacks_.onConfigUpdateFailed(&e);
    }
    ::google::rpc::Status* error_detail = api_state_[type_url].request_.mutable_error_detail();
    error_detail->set_code(Grpc::Status::GrpcStatus::Internal);
    error_detail->set_message(e.what());
  }
  api_state_[type_url].request_.set_response_nonce(message->nonce());
  queueDiscoveryRequest(type_url);
}

void GrpcMuxImpl::handleStreamEstablished() {
  for (const auto type_url : subscriptions_) {
    queueDiscoveryRequest(type_url);
  }
}

void GrpcMuxImpl::handleEstablishmentFailure() {
  for (const auto& api_state : api_state_) {
    for (auto watch : api_state.second.watches_) {
      watch->callbacks_.onConfigUpdateFailed(nullptr);
    }
  }
}

ProtobufTypes::MessagePtr GrpcMuxImpl::dumpControlPlaneConfig() const {
   auto config_dump = std::make_unique<envoy::admin::v2alpha::ControlPlaneConfigDump>();
   std::cout<<"service_control_plane_config_dump_" << service_control_plane_config_dump_.size()<<"\n";
    for (auto& service_info: GrpcMuxImpl::service_control_plane_config_dump_) {
           envoy::admin::v2alpha::ControlPlaneConfigDump::ServiceControlPlaneInfo service_control_plane_info;
           service_control_plane_info.set_service(service_info.first);
           for (auto& config_source_control_info: service_info.second) {
             std::cout<<"before merging..."<<"\n";
             std::string iden = config_source_control_info.control_plane().identifier();
             std::cout<<"here "<<iden<<"\n";
             service_control_plane_info.add_config_source_control_plane().MergeFrom(*config_source_control_info);
                          std::cout<<"after merging..."<<"\n";

           }
     config_dump->add_service_control_plane_info()->MergeFrom(service_control_plane_info);
 }
 return config_dump;
}

} // namespace Config
} // namespace Envoy
