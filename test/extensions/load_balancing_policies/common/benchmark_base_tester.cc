#include "test/extensions/load_balancing_policies/common/benchmark_base_tester.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Common {

BaseTester::BaseTester(uint64_t num_hosts, uint32_t weighted_subset_percent, uint32_t weight,
                       bool attach_metadata) {
  Upstream::HostVector hosts;
  ASSERT(num_hosts < 65536);
  for (uint64_t i = 0; i < num_hosts; i++) {
    const bool should_weight = i < num_hosts * (weighted_subset_percent / 100.0);
    const std::string url = fmt::format("tcp://10.0.{}.{}:6379", i / 256, i % 256);
    const auto effective_weight = should_weight ? weight : 1;
    if (attach_metadata) {
      envoy::config::core::v3::Metadata metadata;
      ProtobufWkt::Value value;
      value.set_number_value(i);
      ProtobufWkt::Struct& map =
          (*metadata.mutable_filter_metadata())[Config::MetadataFilters::get().ENVOY_LB];
      (*map.mutable_fields())[std::string(metadata_key)] = value;

      hosts.push_back(Upstream::makeTestHost(info_, url, metadata, simTime(), effective_weight));
    } else {
      hosts.push_back(Upstream::makeTestHost(info_, url, simTime(), effective_weight));
    }
  }

  Upstream::HostVectorConstSharedPtr updated_hosts = std::make_shared<Upstream::HostVector>(hosts);
  Upstream::HostsPerLocalityConstSharedPtr hosts_per_locality =
      Upstream::makeHostsPerLocality({hosts});
  priority_set_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(updated_hosts, hosts_per_locality), {}, hosts, {},
      absl::nullopt);
  local_priority_set_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(updated_hosts, hosts_per_locality), {}, hosts, {},
      absl::nullopt);
}

} // namespace Common
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
