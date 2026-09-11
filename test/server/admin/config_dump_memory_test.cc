#include <sys/resource.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"
#include "envoy/http/query_params.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/network/utility.h"
#include "source/server/admin/config_dump_handler.h"
#include "source/server/admin/config_tracker_impl.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/mocks/server/admin_stream.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/proto_filler.h"

#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRefOfCopy;

namespace Envoy {
namespace Server {
namespace {

std::string bytesToMiB(uint64_t bytes) {
  return fmt::format("{:.1f}", static_cast<double>(bytes) / (1024 * 1024));
}

constexpr size_t LabelColumns = 2;

void printRow(absl::Span<const std::pair<absl::string_view, std::string>> row) {
  const auto pad = [](absl::string_view text, size_t width, bool left) {
    return left ? fmt::format("{:<{}}", text, width) : fmt::format("{:>{}}", text, width);
  };

  std::string headings;
  std::string rule;
  std::string values;
  for (size_t i = 0; i < row.size(); i++) {
    const auto& [heading, value] = row[i];
    const size_t width = std::max(heading.size(), value.size());
    const bool left = i < LabelColumns;
    absl::StrAppend(&headings, "  ", pad(heading, width, left));
    absl::StrAppend(&rule, "  ", std::string(width, '-'));
    absl::StrAppend(&values, "  ", pad(value, width, left));
  }
  std::cerr << "//" << headings << "\n//" << rule << "\n//" << values << std::endl;
}

uint64_t peakRssBytes() {
  struct rusage usage;
  // reports kilobytes on Linux and bytes on macOS.
  getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
  return static_cast<uint64_t>(usage.ru_maxrss);
#else
  return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
}

struct Scale {
  uint32_t static_clusters_{5};
  uint32_t dynamic_clusters_{30000};
  uint32_t listeners_{10};
  uint32_t static_routes_{15000};
  uint32_t secrets_{3000};
  uint32_t endpoint_clusters_{500};
  uint32_t hosts_per_cluster_{100};
  uint32_t distinct_hosts_{512};
  uint32_t elements_{3};
  uint32_t max_depth_{4};
};

struct Component {
  absl::string_view name_;
  const Protobuf::Message* dump_;
  std::vector<std::pair<absl::string_view, uint32_t>> entries_;
  ProtoFiller::AnyTypes any_types_;
};

std::vector<Component> deployment(const Scale& scale) {
  return {
      Component{"bootstrap", &envoy::admin::v3::BootstrapConfigDump::default_instance(), {}, {}},
      Component{"clusters",
                &envoy::admin::v3::ClustersConfigDump::default_instance(),
                {{"static_clusters", scale.static_clusters_},
                 {"dynamic_active_clusters", scale.dynamic_clusters_}},
                {{"cluster", &envoy::config::cluster::v3::Cluster::default_instance()}}},
      Component{"listeners",
                &envoy::admin::v3::ListenersConfigDump::default_instance(),
                {{"dynamic_listeners", scale.listeners_}},
                {{"listener", &envoy::config::listener::v3::Listener::default_instance()}}},
      Component{
          "routes",
          &envoy::admin::v3::RoutesConfigDump::default_instance(),
          {{"static_route_configs", scale.static_routes_}},
          {{"route_config", &envoy::config::route::v3::RouteConfiguration::default_instance()}}},
      Component{"secrets",
                &envoy::admin::v3::SecretsConfigDump::default_instance(),
                {{"dynamic_active_secrets", scale.secrets_}},
                {{"secret",
                  &envoy::extensions::transport_sockets::tls::v3::Secret::default_instance()}}}};
}

class ConfigDumpMemoryTest : public testing::Test {
protected:
  void SetUp() override {
    Http::Utility::QueryParamsMulti query_params;
    query_params.add("include_eds", "");
    ON_CALL(admin_stream_, queryParams()).WillByDefault(Return(query_params));
  }

  void buildConfig(const Scale& scale) {
    tracker_ = std::make_unique<ConfigTrackerImpl>();

    for (const Component& component : deployment(scale)) {
      const ProtoFiller::Options fill{.elements = scale.elements_,
                                      .max_depth = scale.max_depth_,
                                      .any_types = component.any_types_};
      ProtobufTypes::MessagePtr config(component.dump_->New());
      const Protobuf::Reflection& reflection = *config->GetReflection();
      if (component.entries_.empty()) {
        ProtoFiller::fill(*config, fill);
      }
      for (const auto& [field, count] : component.entries_) {
        const Protobuf::FieldDescriptor& entries =
            *config->GetDescriptor()->FindFieldByName(std::string(field));
        if (count == 0) {
          continue;
        }
        ProtoFiller::fill(*reflection.AddMessage(config.get(), &entries), fill);
        const Protobuf::Message& resource = reflection.GetRepeatedMessage(*config, &entries, 0);
        for (uint32_t i = 1; i < count; i++) {
          reflection.AddMessage(config.get(), &entries)->CopyFrom(resource);
        }
      }
      tracked_.emplace_back(component.name_, std::move(config));

      const Protobuf::Message& source = *tracked_.back().second;
      entries_.push_back(
          tracker_->add(std::string(component.name_), [&source](const Matchers::StringMatcher&) {
            ProtobufTypes::MessagePtr copy(source.New());
            copy->CopyFrom(source);
            return copy;
          }));
    }

    wireClusterManager(scale);
    handler_ = std::make_unique<ConfigDumpHandler>(*tracker_, server_);
  }

  struct ConfigSizes {
    uint64_t total_{0};
    uint64_t largest_component_{0};
  };

  ConfigSizes configSizes() const {
    ConfigSizes sizes;
    for (const auto& [name, config] : tracked_) {
      const uint64_t bytes = config->ByteSizeLong();
      sizes.total_ += bytes;
      sizes.largest_component_ = std::max(sizes.largest_component_, bytes);
    }
    return sizes;
  }

  struct Streamed {
    uint64_t peak_consumed_{0};
    uint64_t response_bytes_{0};
    uint64_t chunks_{0};
  };

  Streamed stream() {
    Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
    Buffer::OwnedImpl chunk;
    Streamed streamed;

    Memory::TestUtil::MemoryTest memory_test;
    Admin::RequestPtr request = handler_->makeRequest(admin_stream_);
    request->start(*headers);
    for (bool more = true; more;) {
      more = request->nextChunk(chunk);
      streamed.peak_consumed_ =
          std::max(streamed.peak_consumed_, static_cast<uint64_t>(memory_test.consumedBytes()));
      streamed.response_bytes_ += chunk.length();
      streamed.chunks_++;
      chunk.drain(chunk.length());
    }
    return streamed;
  }

private:
  void wireClusterManager(const Scale& scale) {
    const std::vector<Upstream::HostSharedPtr> hosts = buildHostPool(scale);

    for (uint32_t i = 0; i < scale.endpoint_clusters_; i++) {
      auto cluster = std::make_unique<NiceMock<Upstream::MockClusterMockPrioritySet>>();
      cluster->info_->name_ = absl::StrCat("tenant-", i, "-upstream");
      ON_CALL(*cluster->info_, addedViaApi()).WillByDefault(Return(i % 4 != 0));
      ON_CALL(*cluster, dropOverload()).WillByDefault(Return(UnitFloat(0.00035)));

      Upstream::MockHostSet* host_set = cluster->priority_set_.getMockHostSet(0);
      for (uint32_t j = 0; j < scale.hosts_per_cluster_; j++) {
        host_set->hosts_.emplace_back(hosts[(i * scale.hosts_per_cluster_ + j) % hosts.size()]);
      }

      cluster_maps_.active_clusters_.emplace(cluster->info_->name_, *cluster);
      clusters_.push_back(std::move(cluster));
    }

    ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_maps_));
    ON_CALL(server_.cluster_manager_, hasActiveClusters()).WillByDefault(Return(true));
    ON_CALL(server_.cluster_manager_, getActiveCluster(_))
        .WillByDefault(Invoke([this](absl::string_view name) -> OptRef<const Upstream::Cluster> {
          const auto at = cluster_maps_.active_clusters_.find(name);
          if (at == cluster_maps_.active_clusters_.end()) {
            return std::nullopt;
          }
          return at->second.get();
        }));
  }

  std::vector<Upstream::HostSharedPtr> buildHostPool(const Scale& scale) {
    std::vector<Upstream::HostSharedPtr> hosts;
    hosts.reserve(scale.distinct_hosts_);
    for (uint32_t i = 0; i < scale.distinct_hosts_; i++) {
      hosts.push_back(makeHost(i, scale));
    }
    return hosts;
  }

  Upstream::HostSharedPtr makeHost(uint32_t index, const Scale& scale) {
    auto host = std::make_shared<NiceMock<Upstream::MockHost>>();

    const std::string hostname = absl::StrCat("backend-", index, ".example.com");

    envoy::config::core::v3::Locality locality;
    ProtoFiller::fill(locality, {.elements = scale.elements_, .max_depth = scale.max_depth_});

    auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
    ProtoFiller::fill(*metadata, {.elements = scale.elements_, .max_depth = 2});

    const uint32_t high = index / 256 % 256;
    const uint32_t low = index % 256;
    const Network::Address::InstanceConstSharedPtr address =
        *Network::Utility::resolveUrl(absl::StrCat("tcp://10.0.", high, ".", low, ":8443"));
    auto address_list = std::make_shared<Upstream::HostDescription::AddressVector>();
    address_list->push_back(address);
    address_list->push_back(
        *Network::Utility::resolveUrl(absl::StrCat("tcp://172.16.", high, ".", low, ":8443")));

    ON_CALL(*host, locality()).WillByDefault(ReturnRefOfCopy(locality));
    ON_CALL(*host, hostname()).WillByDefault(ReturnRefOfCopy(hostname));
    ON_CALL(*host, hostnameForHealthChecks())
        .WillByDefault(ReturnRefOfCopy(absl::StrCat(hostname, ".health")));
    ON_CALL(*host, address()).WillByDefault(Return(address));
    ON_CALL(*host, addressListOrNull()).WillByDefault(Return(address_list));
    ON_CALL(*host, healthCheckAddress()).WillByDefault(Return(address));
    ON_CALL(*host, metadata()).WillByDefault(Return(metadata));
    ON_CALL(*host, weight()).WillByDefault(Return(index % 100 + 1));
    ON_CALL(*host, priority()).WillByDefault(Return(0));
    ON_CALL(*host, coarseHealth())
        .WillByDefault(Return(index % 8 == 0 ? Upstream::Host::Health::Degraded
                                             : Upstream::Host::Health::Healthy));

    return host;
  }

protected:
  NiceMock<MockInstance> server_;
  NiceMock<MockAdminStream> admin_stream_;
  std::unique_ptr<ConfigTrackerImpl> tracker_;
  std::vector<std::pair<std::string, ProtobufTypes::MessagePtr>> tracked_;
  std::vector<ConfigTracker::EntryOwnerPtr> entries_;
  std::unique_ptr<ConfigDumpHandler> handler_;

  Upstream::ClusterManager::ClusterInfoMaps cluster_maps_;
  std::vector<std::unique_ptr<NiceMock<Upstream::MockClusterMockPrioritySet>>> clusters_;
};

TEST_F(ConfigDumpMemoryTest, BoundedMemory) {
  // SPELLCHECKER(off)
  // clang-format off
  //
  // TODO(filipcacky): remeasure this in CI, current values are from a dev VM
  //
  // Date        PR     clusters  listeners  routes  secrets  endpoints  elements  max_depth  config MiB  largest component MiB  chunk size  peak MiB  rss MiB  response MiB  chunks
  // ----------  -----  --------  ---------  ------  -------  ---------  --------  ---------  ----------  ---------------------  ----------  --------  -------  ------------  ------
  // 2026/08/27  #####     30005         10   15000     3000      50000         3          4       188.6                  104.7           -    2950.0   3564.6        1217.1       -
  // 2026/08/27  #####     30005         10   15000     3000      50000         3          4       188.6                  104.7       65536     135.4    140.5         746.4   11941
  //
  // clang-format on
  // SPELLCHECKER(on)

  const Scale scale;
  buildConfig(scale);
  const uint64_t rss_after_config = peakRssBytes();

  const Streamed streamed = stream();

  const uint64_t peak_rss = peakRssBytes();
  const ConfigSizes sizes = configSizes();

  printRow({{"Date", "----------"},
            {"PR", "#####"},
            {"clusters", absl::StrCat(scale.static_clusters_ + scale.dynamic_clusters_)},
            {"listeners", absl::StrCat(scale.listeners_)},
            {"routes", absl::StrCat(scale.static_routes_)},
            {"secrets", absl::StrCat(scale.secrets_)},
            {"endpoints", absl::StrCat(scale.endpoint_clusters_ * scale.hosts_per_cluster_)},
            {"elements", absl::StrCat(scale.elements_)},
            {"max_depth", absl::StrCat(scale.max_depth_)},
            {"config MiB", bytesToMiB(sizes.total_)},
            {"largest component MiB", bytesToMiB(sizes.largest_component_)},
            {"chunk size", absl::StrCat(ConfigDumpRequest::DefaultChunkSize)},
            {"peak MiB", bytesToMiB(streamed.peak_consumed_)},
            {"rss MiB", bytesToMiB(peak_rss - rss_after_config)},
            {"response MiB", bytesToMiB(streamed.response_bytes_)},
            {"chunks", absl::StrCat(streamed.chunks_)}});

  EXPECT_MEMORY_LE(streamed.peak_consumed_, 2 * sizes.largest_component_);
}

} // namespace
} // namespace Server
} // namespace Envoy
