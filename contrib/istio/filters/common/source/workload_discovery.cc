#include "contrib/istio/filters/common/source/workload_discovery.h"

#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/non_copyable.h"
#include "source/common/config/subscription_base.h"
#include "source/common/grpc/common.h"
#include "source/common/init/target_impl.h"

#include "contrib/envoy/extensions/filters/common/workload_discovery/v3/discovery.pb.h"
#include "contrib/envoy/extensions/filters/common/workload_discovery/v3/discovery.pb.validate.h"
#include "contrib/envoy/extensions/filters/common/workload_discovery/v3/extension.pb.h"
#include "contrib/envoy/extensions/filters/common/workload_discovery/v3/extension.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace WorkloadDiscovery {

namespace {
constexpr absl::string_view DefaultNamespace = "default";
constexpr absl::string_view DefaultServiceAccount = "default";
constexpr absl::string_view DefaultTrustDomain = "cluster.local";

Istio::Common::WorkloadMetadataObject convert(const istio::workload::Workload& workload) {
  auto workload_type = Istio::Common::WorkloadType::Deployment;
  switch (workload.workload_type()) {
  case istio::workload::WorkloadType::CRONJOB:
    workload_type = Istio::Common::WorkloadType::CronJob;
    break;
  case istio::workload::WorkloadType::JOB:
    workload_type = Istio::Common::WorkloadType::Job;
    break;
  case istio::workload::WorkloadType::POD:
    workload_type = Istio::Common::WorkloadType::Pod;
    break;
  default:
    break;
  }

  absl::string_view ns = workload.namespace_();
  absl::string_view trust_domain = workload.trust_domain();
  absl::string_view service_account = workload.service_account();
  // Trust domain may be elided if it's equal to "cluster.local"
  if (trust_domain.empty()) {
    trust_domain = DefaultTrustDomain;
  }
  // The namespace may be elided if it's equal to "default"
  if (ns.empty()) {
    ns = DefaultNamespace;
  }
  // The service account may be elided if it's equal to "default"
  if (service_account.empty()) {
    service_account = DefaultServiceAccount;
  }
  const auto identity =
      absl::StrCat("spiffe://", trust_domain, "/ns/", ns, "/sa/", service_account);
  return Istio::Common::WorkloadMetadataObject(
      workload.name(), workload.cluster_id(), ns, workload.workload_name(),
      workload.canonical_name(), workload.canonical_revision(), workload.canonical_name(),
      workload.canonical_revision(), workload_type, identity);
}
} // namespace

class WorkloadMetadataProviderImpl : public WorkloadMetadataProvider, public Singleton::Instance {
public:
  WorkloadMetadataProviderImpl(const envoy::config::core::v3::ConfigSource& config_source,
                               Server::Configuration::ServerFactoryContext& factory_context)
      : config_source_(config_source), factory_context_(factory_context),
        tls_(factory_context.threadLocal()),
        scope_(factory_context.scope().createScope("workload_discovery")),
        stats_(generateStats(*scope_)), subscription_(*this) {
    tls_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalProvider>(); });
    // This is safe because the ADS mux is started in the cluster manager constructor prior to this
    // call.
    subscription_.start();
  }

  absl::optional<Istio::Common::WorkloadMetadataObject>
  getMetadata(const Network::Address::InstanceConstSharedPtr& address) override {
    if (address && address->ip()) {
      if (const auto ipv4 = address->ip()->ipv4(); ipv4) {
        uint32_t value = ipv4->address();
        std::array<uint8_t, 4> output;
        absl::little_endian::Store32(&output, value);
        return tls_->get(std::string(output.begin(), output.end()));
      } else if (const auto ipv6 = address->ip()->ipv6(); ipv6) {
        const uint64_t high = absl::Uint128High64(ipv6->address());
        const uint64_t low = absl::Uint128Low64(ipv6->address());
        std::array<uint8_t, 16> output;
        absl::little_endian::Store64(&output, low);
        absl::little_endian::Store64(&output[8], high);
        return tls_->get(std::string(output.begin(), output.end()));
      }
    }
    return {};
  }

private:
  using IdToAddress = absl::flat_hash_map<std::string, std::vector<std::string>>;
  using IdToAddressSharedPtr = std::shared_ptr<IdToAddress>;
  using AddressToWorkload = absl::flat_hash_map<std::string, Istio::Common::WorkloadMetadataObject>;
  using AddressToWorkloadSharedPtr = std::shared_ptr<AddressToWorkload>;

  struct ThreadLocalProvider : public ThreadLocal::ThreadLocalObject {
    void reset(const AddressToWorkloadSharedPtr& index) { address_to_workload_ = *index; }
    void update(const AddressToWorkloadSharedPtr& added_addresses,
                const IdToAddressSharedPtr& added_ids,
                const std::shared_ptr<std::vector<std::string>> removed) {
      for (const auto& id : *removed) {
        for (const auto& address : id_to_address_[id]) {
          address_to_workload_.erase(address);
        }
        id_to_address_.erase(id);
      }
      for (const auto& [address, workload] : *added_addresses) {
        address_to_workload_.emplace(address, workload);
      }
      for (const auto& [id, address] : *added_ids) {
        id_to_address_.emplace(id, address);
      }
    }
    size_t total() const { return address_to_workload_.size(); }
    // Returns by-value since the flat map does not provide pointer stability.
    absl::optional<Istio::Common::WorkloadMetadataObject> get(const std::string& address) {
      const auto it = address_to_workload_.find(address);
      if (it != address_to_workload_.end()) {
        return it->second;
      }
      return {};
    }
    IdToAddress id_to_address_;
    AddressToWorkload address_to_workload_;
  };
  class WorkloadSubscription : Config::SubscriptionBase<istio::workload::Workload> {
  public:
    WorkloadSubscription(WorkloadMetadataProviderImpl& parent)
        : Config::SubscriptionBase<istio::workload::Workload>(
              parent.factory_context_.messageValidationVisitor(), "uid"),
          parent_(parent) {
      subscription_ = THROW_OR_RETURN_VALUE(
          parent.factory_context_.clusterManager()
              .subscriptionFactory()
              .subscriptionFromConfigSource(parent.config_source_,
                                            Grpc::Common::typeUrl(getResourceName()),
                                            *parent.scope_, *this, resource_decoder_, {}),
          Config::SubscriptionPtr);
    }
    void start() { subscription_->start({}); }

  private:
    // Config::SubscriptionCallbacks
    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                const std::string&) override {
      AddressToWorkloadSharedPtr index = std::make_shared<AddressToWorkload>();
      for (const auto& resource : resources) {
        const auto& workload =
            dynamic_cast<const istio::workload::Workload&>(resource.get().resource());
        const auto& metadata = convert(workload);
        for (const auto& addr : workload.addresses()) {
          index->emplace(addr, metadata);
        }
      }
      parent_.reset(index);
      return absl::OkStatus();
    }
    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                const std::string&) override {
      IdToAddressSharedPtr added_ids = std::make_shared<IdToAddress>();
      AddressToWorkloadSharedPtr added_addresses = std::make_shared<AddressToWorkload>();
      for (const auto& resource : added_resources) {
        const auto& workload =
            dynamic_cast<const istio::workload::Workload&>(resource.get().resource());
        const auto& metadata = convert(workload);
        for (const auto& addr : workload.addresses()) {
          added_addresses->emplace(addr, metadata);
        }
        added_ids->emplace(workload.uid(), std::vector<std::string>(workload.addresses().begin(),
                                                                    workload.addresses().end()));
      }
      auto removed = std::make_shared<std::vector<std::string>>();
      removed->reserve(removed_resources.size());
      for (const auto& resource : removed_resources) {
        removed->push_back(resource);
      }
      parent_.update(added_addresses, added_ids, removed);
      return absl::OkStatus();
    }
    void onConfigUpdateFailed(Config::ConfigUpdateFailureReason, const EnvoyException*) override {
      // Do nothing - feature is automatically disabled.
      // TODO: Potential issue with the expiration of the metadata.
    }
    WorkloadMetadataProviderImpl& parent_;
    Config::SubscriptionPtr subscription_;
  };

  void reset(AddressToWorkloadSharedPtr index) {
    tls_.runOnAllThreads([index](OptRef<ThreadLocalProvider> tls) { tls->reset(index); });
    stats_.total_.set(tls_->total());
  }

  void update(const AddressToWorkloadSharedPtr& added_addresses,
              const IdToAddressSharedPtr& added_ids,
              const std::shared_ptr<std::vector<std::string>> removed) {
    tls_.runOnAllThreads([added_addresses, added_ids, removed](OptRef<ThreadLocalProvider> tls) {
      tls->update(added_addresses, added_ids, removed);
    });
    stats_.total_.set(tls_->total());
  }

  WorkloadDiscoveryStats generateStats(Stats::Scope& scope) {
    return WorkloadDiscoveryStats{WORKLOAD_DISCOVERY_STATS(POOL_GAUGE(scope))};
  }

  const envoy::config::core::v3::ConfigSource config_source_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  ThreadLocal::TypedSlot<ThreadLocalProvider> tls_;
  Stats::ScopeSharedPtr scope_;
  WorkloadDiscoveryStats stats_;
  WorkloadSubscription subscription_;
};

SINGLETON_MANAGER_REGISTRATION(workload_metadata_provider)

class WorkloadDiscoveryExtension : public Server::BootstrapExtension {
public:
  WorkloadDiscoveryExtension(Server::Configuration::ServerFactoryContext& factory_context,
                             const istio::workload::BootstrapExtension& config)
      : factory_context_(factory_context), config_(config) {}

  // Server::Configuration::BootstrapExtension
  void onServerInitialized() override {
    provider_ = factory_context_.singletonManager().getTyped<WorkloadMetadataProvider>(
        SINGLETON_MANAGER_REGISTERED_NAME(workload_metadata_provider), [&] {
          return std::make_shared<WorkloadMetadataProviderImpl>(config_.config_source(),
                                                                factory_context_);
        });
  }

  void onWorkerThreadInitialized() override {};

private:
  Server::Configuration::ServerFactoryContext& factory_context_;
  const istio::workload::BootstrapExtension config_;
  WorkloadMetadataProviderSharedPtr provider_;
};

class WorkloadDiscoveryFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override {
    const auto& message =
        MessageUtil::downcastAndValidate<const istio::workload::BootstrapExtension&>(
            config, context.messageValidationVisitor());
    return std::make_unique<WorkloadDiscoveryExtension>(context, message);
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<istio::workload::BootstrapExtension>();
  }
  std::string name() const override { return "envoy.bootstrap.workload_discovery"; };
};

REGISTER_FACTORY(WorkloadDiscoveryFactory, Server::Configuration::BootstrapExtensionFactory);

WorkloadMetadataProviderSharedPtr
getProvider(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<WorkloadMetadataProvider>(
      SINGLETON_MANAGER_REGISTERED_NAME(workload_metadata_provider));
}

} // namespace WorkloadDiscovery
} // namespace Common
} // namespace Extensions
} // namespace Envoy
