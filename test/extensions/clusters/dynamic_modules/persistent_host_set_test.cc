#include <algorithm>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/dynamic_modules/persistent_host_set.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/cluster_info.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using Extensions::Clusters::DynamicModules::PersistentHostSet;
using testing::NiceMock;

// Keeps the expected membership next to a MainPrioritySetImpl whose host sets are
// PersistentHostSet, and checks every view against a full partitionHosts pass over that membership.
class PersistentHostPartitionsTest : public testing::Test {
protected:
  PersistentHostPartitionsTest() {
    // The factory is installed before any host set exists, as ClusterImplBase does.
    priority_set_.setHostSetFactory(&PersistentHostSet::create);
    priority_set_.getOrCreateHostSet(0);
  }

  PersistentHostSet& hostSet(uint32_t priority) {
    priority_set_.getOrCreateHostSet(priority);
    return static_cast<PersistentHostSet&>(*priority_set_.hostSetsPerPriority()[priority]);
  }

  // Applies a membership delta the way the dynamic-modules cluster does: patch the host set, then
  // hand its snapshot to the priority set with the same delta.
  void delta(uint32_t priority, const HostVector& added, const HostVector& removed) {
    auto snapshot = hostSet(priority).applyMembershipDelta(added, removed);
    priority_set_.updateHosts(priority, HostSetImpl::snapshotUpdateHostsParams(snapshot), nullptr,
                              added, removed, std::nullopt, std::nullopt);
    EXPECT_EQ(snapshot, priority_set_.hostSetsPerPriority()[priority]->snapshot());
  }

  // Applies a health change the same way. Returns false if the host set does not hold `host`.
  bool healthChange(uint32_t priority, const HostSharedPtr& host) {
    if (priority >= priority_set_.hostSetsPerPriority().size()) {
      return false;
    }
    auto snapshot = hostSet(priority).applyHostHealthChange(host);
    if (snapshot == nullptr) {
      return false;
    }
    priority_set_.updateHosts(priority, HostSetImpl::snapshotUpdateHostsParams(snapshot), nullptr,
                              {}, {}, std::nullopt, std::nullopt);
    return true;
  }

  HostSharedPtr makeHost(uint32_t index, const std::string& zone) {
    envoy::config::core::v3::Locality locality;
    locality.set_zone(zone);
    return makeTestHost(info_, fmt::format("tcp://10.0.{}.{}:80", index / 200, index % 200),
                        locality);
  }

  // Groups `hosts` into one bucket per locality, as dynamic-modules clusters do.
  static HostsPerLocalityConstSharedPtr groupByLocality(const HostVector& hosts) {
    std::map<std::string, HostVector> by_zone;
    for (const auto& host : hosts) {
      by_zone[host->locality().zone()].push_back(host);
    }
    std::vector<HostVector> buckets;
    for (auto& [_, bucket] : by_zone) {
      buckets.push_back(std::move(bucket));
    }
    return std::make_shared<HostsPerLocalityImpl>(std::move(buckets), false);
  }

  static std::vector<std::string> addresses(const HostVector& hosts) {
    std::vector<std::string> out;
    out.reserve(hosts.size());
    for (const auto& host : hosts) {
      out.push_back(host->address()->asString());
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  // Non-empty buckets keyed by zone. Bucket order is not part of the contract.
  static std::map<std::string, std::vector<std::string>>
  byZone(const HostsPerLocality& per_locality) {
    std::map<std::string, std::vector<std::string>> out;
    for (const auto& bucket : per_locality.get()) {
      if (!bucket.empty()) {
        out[bucket.front()->locality().zone()] = addresses(bucket);
      }
    }
    return out;
  }

  void expectMatchesFullPartition() {
    const HostSet& host_set = *priority_set_.hostSetsPerPriority()[0];
    const auto expected = HostSetImpl::partitionHosts(std::make_shared<HostVector>(members_),
                                                      groupByLocality(members_));
    // Counts first: they must be right without the flat views being built.
    EXPECT_EQ(expected.hosts->size(), host_set.hostCounts().hosts);
    EXPECT_EQ(expected.healthy_hosts->get().size(), host_set.hostCounts().healthy);
    EXPECT_EQ(expected.degraded_hosts->get().size(), host_set.hostCounts().degraded);
    EXPECT_EQ(expected.excluded_hosts->get().size(), host_set.hostCounts().excluded);

    EXPECT_EQ(addresses(*expected.hosts), addresses(host_set.hosts()));
    EXPECT_EQ(addresses(expected.healthy_hosts->get()), addresses(host_set.healthyHosts()));
    EXPECT_EQ(addresses(expected.degraded_hosts->get()), addresses(host_set.degradedHosts()));
    EXPECT_EQ(addresses(expected.excluded_hosts->get()), addresses(host_set.excludedHosts()));
    EXPECT_EQ(byZone(*expected.hosts_per_locality), byZone(host_set.hostsPerLocality()));
    EXPECT_EQ(byZone(*expected.healthy_hosts_per_locality),
              byZone(host_set.healthyHostsPerLocality()));
    EXPECT_EQ(byZone(*expected.degraded_hosts_per_locality),
              byZone(host_set.degradedHostsPerLocality()));
    EXPECT_EQ(byZone(*expected.excluded_hosts_per_locality),
              byZone(host_set.excludedHostsPerLocality()));

    const auto lookup = priority_set_.crossPriorityHostMap();
    EXPECT_EQ(members_.size(), lookup->size());
    for (const auto& host : members_) {
      EXPECT_EQ(host, lookup->findHost(host->address()->asString()));
    }
  }

  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  MainPrioritySetImpl priority_set_;
  HostVector members_;
};

// Every priority of an opted-in priority set is a PersistentHostSet.
TEST_F(PersistentHostPartitionsTest, OptInCreatesPersistentHostSets) {
  EXPECT_NE(nullptr,
            dynamic_cast<const PersistentHostSet*>(priority_set_.hostSetsPerPriority()[0].get()));
  priority_set_.getOrCreateHostSet(2);
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    EXPECT_NE(nullptr, dynamic_cast<const PersistentHostSet*>(host_set.get()));
  }
}

// Membership deltas run the member and priority callbacks with exactly the delta, and a health
// change runs them with an empty delta.
TEST_F(PersistentHostPartitionsTest, CallbacksCarryTheDelta) {
  std::vector<std::pair<size_t, size_t>> member_updates;
  auto member_cb =
      priority_set_.addMemberUpdateCb([&](const HostVector& added, const HostVector& removed) {
        member_updates.emplace_back(added.size(), removed.size());
      });
  std::vector<uint32_t> priority_updates;
  auto priority_cb = priority_set_.addPriorityUpdateCb(
      [&](uint32_t priority, const HostVector&, const HostVector&) {
        priority_updates.push_back(priority);
      });

  const HostVector added{makeHost(1, "a"), makeHost(2, "a"), makeHost(3, "b")};
  delta(0, added, {});
  delta(0, {}, {added[0]});
  added[1]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_TRUE(healthChange(0, added[1]));

  EXPECT_EQ((std::vector<std::pair<size_t, size_t>>{{3, 0}, {0, 1}, {0, 0}}), member_updates);
  EXPECT_EQ((std::vector<uint32_t>{0, 0, 0}), priority_updates);
  members_ = {added[1], added[2]};
  expectMatchesFullPartition();
}

// A health change for a host that is not in the set changes nothing and runs no callbacks.
TEST_F(PersistentHostPartitionsTest, HealthChangeForNonMember) {
  delta(0, {makeHost(1, "a")}, {});
  size_t member_updates = 0;
  auto member_cb = priority_set_.addMemberUpdateCb(
      [&](const HostVector&, const HostVector&) { ++member_updates; });
  EXPECT_FALSE(healthChange(0, makeHost(2, "a")));
  EXPECT_FALSE(healthChange(5, makeHost(2, "a")));
  EXPECT_EQ(0, member_updates);
}

// A health refresh re-partitions every host in place, keeps the host set on its own snapshot, and
// reports the health-changed hosts as unknown.
TEST_F(PersistentHostPartitionsTest, RefreshHealth) {
  members_ = {makeHost(1, "a"), makeHost(2, "a"), makeHost(3, "b")};
  delta(0, members_, {});
  members_[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  members_[2]->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);

  auto snapshot = hostSet(0).refreshHealth();
  priority_set_.updateHosts(0, HostSetImpl::snapshotUpdateHostsParams(snapshot), nullptr, {}, {},
                            std::nullopt, std::nullopt);
  EXPECT_EQ(snapshot, priority_set_.hostSetsPerPriority()[0]->snapshot());
  EXPECT_FALSE(hostSet(0).healthChangedHosts().has_value());
  expectMatchesFullPartition();
}

// Emptying a locality drops its bucket, and a later host in that locality recreates it.
TEST_F(PersistentHostPartitionsTest, LocalityBucketsFollowMembership) {
  const HostSharedPtr a1 = makeHost(1, "a");
  const HostSharedPtr b1 = makeHost(2, "b");
  delta(0, {a1, b1}, {});
  EXPECT_EQ(2, priority_set_.hostSetsPerPriority()[0]->hostsPerLocality().get().size());

  delta(0, {}, {a1});
  EXPECT_EQ(1, priority_set_.hostSetsPerPriority()[0]->hostsPerLocality().get().size());

  const HostSharedPtr a2 = makeHost(3, "a");
  delta(0, {a2}, {});
  members_ = {b1, a2};
  expectMatchesFullPartition();
}

// Random membership deltas, health changes and occasional full updates keep every view equal to a
// full pass over the current membership.
TEST_F(PersistentHostPartitionsTest, RandomDeltasMatchFullPartition) {
  const std::vector<std::string> zones{"a", "b", "c"};
  HostVector pool;
  for (uint32_t i = 0; i < 120; ++i) {
    pool.push_back(makeHost(i, zones[i % zones.size()]));
  }
  const std::vector<Host::HealthFlag> flags{
      Host::HealthFlag::FAILED_ACTIVE_HC, Host::HealthFlag::DEGRADED_ACTIVE_HC,
      Host::HealthFlag::PENDING_ACTIVE_HC, Host::HealthFlag::FAILED_OUTLIER_CHECK};

  std::mt19937 rng(42);
  const auto pick = [&](size_t bound) {
    return std::uniform_int_distribution<size_t>(0, bound - 1)(rng);
  };
  const auto is_member = [&](const HostSharedPtr& host) {
    return std::find(members_.begin(), members_.end(), host) != members_.end();
  };

  for (int step = 0; step < 400; ++step) {
    const size_t op = pick(100);
    if (op < 35) {
      HostVector added;
      for (size_t n = 1 + pick(5); n > 0; --n) {
        const HostSharedPtr& host = pool[pick(pool.size())];
        if (!is_member(host) && std::find(added.begin(), added.end(), host) == added.end()) {
          added.push_back(host);
        }
      }
      delta(0, added, {});
      members_.insert(members_.end(), added.begin(), added.end());
    } else if (op < 60 && !members_.empty()) {
      HostVector removed;
      for (size_t n = 1 + pick(5); n > 0 && !members_.empty(); --n) {
        const size_t index = pick(members_.size());
        removed.push_back(members_[index]);
        members_.erase(members_.begin() + index);
      }
      delta(0, {}, removed);
    } else if (op < 95 && !members_.empty()) {
      const HostSharedPtr& host = members_[pick(members_.size())];
      const Host::HealthFlag flag = flags[pick(flags.size())];
      if (host->healthFlagGet(flag)) {
        host->healthFlagClear(flag);
      } else {
        host->healthFlagSet(flag);
      }
      // The health checker only sets PENDING_ACTIVE_HC together with FAILED_ACTIVE_HC.
      if (host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC)) {
        host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
      }
      EXPECT_TRUE(healthChange(0, host));
    } else {
      // A full update makes the given vectors authoritative; later deltas must build on them.
      priority_set_.updateHosts(0,
                                HostSetImpl::partitionHosts(std::make_shared<HostVector>(members_),
                                                            groupByLocality(members_)),
                                nullptr, {}, {}, std::nullopt, std::nullopt);
    }
    expectMatchesFullPartition();
  }
}

// A delta publishes a snapshot of the partitions, and a full update publishes none because its flat
// views are already current. Flat host sets never publish one.
TEST_F(PersistentHostPartitionsTest, SnapshotFollowsTheCurrentRepresentation) {
  const HostSet& host_set = *priority_set_.hostSetsPerPriority()[0];
  EXPECT_EQ(nullptr, host_set.snapshot());

  members_ = {makeHost(1, "a"), makeHost(2, "b")};
  delta(0, members_, {});
  EXPECT_NE(nullptr, host_set.snapshot());

  priority_set_.updateHosts(0,
                            HostSetImpl::partitionHosts(std::make_shared<HostVector>(members_),
                                                        groupByLocality(members_)),
                            nullptr, {}, {}, std::nullopt, std::nullopt);
  EXPECT_EQ(nullptr, host_set.snapshot());

  HostSetImpl flat_host_set(0, std::nullopt, std::nullopt);
  EXPECT_EQ(nullptr, flat_host_set.snapshot());
}

// A worker adopting snapshots matches the main-thread partitions, and an adopted view does not
// change when the main thread moves on.
TEST_F(PersistentHostPartitionsTest, WorkerAdoptsSnapshots) {
  PrioritySetImpl worker;
  worker.setHostSetFactory(&PersistentHostSet::create);
  worker.getOrCreateHostSet(0);
  std::vector<std::pair<size_t, size_t>> worker_updates;
  auto worker_cb =
      worker.addMemberUpdateCb([&](const HostVector& added, const HostVector& removed) {
        worker_updates.emplace_back(added.size(), removed.size());
      });
  const auto publish = [&](const HostVector& added, const HostVector& removed) {
    worker.updateHosts(
        0,
        HostSetImpl::snapshotUpdateHostsParams(priority_set_.hostSetsPerPriority()[0]->snapshot()),
        nullptr, added, removed, std::nullopt, std::nullopt);
  };

  const HostVector first{makeHost(1, "a"), makeHost(2, "a"), makeHost(3, "b")};
  delta(0, first, {});
  publish(first, {});
  const HostSet& worker_set = *worker.hostSetsPerPriority()[0];
  ASSERT_NE(nullptr, dynamic_cast<const PersistentHostSet*>(&worker_set));
  EXPECT_EQ(3, worker_set.hostCounts().hosts);
  EXPECT_EQ(3, worker_set.hostCounts().healthy);
  const HostVector adopted_hosts = worker_set.hosts();

  first[1]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_TRUE(healthChange(0, first[1]));
  const HostVector second{makeHost(4, "c")};
  delta(0, second, {first[0]});
  // The adopted snapshot is unchanged by the main thread's later deltas. Counts read the snapshot
  // itself, and the main thread now has 2 healthy hosts.
  EXPECT_EQ(3, worker_set.hostCounts().healthy);
  EXPECT_EQ(2, priority_set_.hostSetsPerPriority()[0]->hostCounts().healthy);
  EXPECT_EQ(addresses(adopted_hosts), addresses(worker_set.hosts()));

  publish(second, {first[0]});
  members_ = {first[1], first[2], second[0]};
  const auto expected = HostSetImpl::partitionHosts(std::make_shared<HostVector>(members_),
                                                    groupByLocality(members_));
  EXPECT_EQ(3, worker_set.hostCounts().hosts);
  EXPECT_EQ(2, worker_set.hostCounts().healthy);
  EXPECT_EQ(0, worker_set.hostCounts().degraded);
  EXPECT_EQ(0, worker_set.hostCounts().excluded);
  EXPECT_EQ(addresses(*expected.hosts), addresses(worker_set.hosts()));
  EXPECT_EQ(addresses(expected.healthy_hosts->get()), addresses(worker_set.healthyHosts()));
  EXPECT_EQ(byZone(*expected.hosts_per_locality), byZone(worker_set.hostsPerLocality()));
  EXPECT_EQ(byZone(*expected.healthy_hosts_per_locality),
            byZone(worker_set.healthyHostsPerLocality()));
  EXPECT_EQ((std::vector<std::pair<size_t, size_t>>{{3, 0}, {1, 1}}), worker_updates);
}

// The shared pointer accessors return the same views as the reference accessors.
TEST_F(PersistentHostPartitionsTest, PointerAccessorsMatchViews) {
  members_ = {makeHost(1, "a"), makeHost(2, "b")};
  members_[1]->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  delta(0, members_, {});
  const HostSet& host_set = *priority_set_.hostSetsPerPriority()[0];
  EXPECT_EQ(&host_set.hosts(), host_set.hostsPtr().get());
  EXPECT_EQ(&host_set.healthyHosts(), &host_set.healthyHostsPtr()->get());
  EXPECT_EQ(&host_set.degradedHosts(), &host_set.degradedHostsPtr()->get());
  EXPECT_EQ(&host_set.excludedHosts(), &host_set.excludedHostsPtr()->get());
  EXPECT_EQ(&host_set.hostsPerLocality(), host_set.hostsPerLocalityPtr().get());
  EXPECT_EQ(&host_set.healthyHostsPerLocality(), host_set.healthyHostsPerLocalityPtr().get());
  EXPECT_EQ(&host_set.degradedHostsPerLocality(), host_set.degradedHostsPerLocalityPtr().get());
  EXPECT_EQ(&host_set.excludedHostsPerLocality(), host_set.excludedHostsPerLocalityPtr().get());
  EXPECT_EQ(1, host_set.hostCounts().degraded);
  EXPECT_EQ(0, host_set.hostCounts().excluded);
}

// Removing a host whose locality has no bucket, or after a full update without a per-locality
// layout, leaves the views equal to a full pass.
TEST_F(PersistentHostPartitionsTest, RemoveWithoutLocalityBucket) {
  members_ = {makeHost(1, "a"), makeHost(2, "b")};
  delta(0, members_, {});
  delta(0, {}, {makeHost(3, "z")});
  expectMatchesFullPartition();

  priority_set_.updateHosts(0,
                            HostSetImpl::partitionHosts(std::make_shared<HostVector>(members_),
                                                        HostsPerLocalityImpl::empty()),
                            nullptr, {}, {}, std::nullopt, std::nullopt);
  delta(0, {}, {members_[0]});
  const HostSet& host_set = *priority_set_.hostSetsPerPriority()[0];
  EXPECT_EQ(1, host_set.hostCounts().hosts);
  EXPECT_EQ(addresses({members_[1]}), addresses(host_set.hosts()));
}

// Each update records which hosts' health changed, and snapshots carry that to workers.
TEST_F(PersistentHostPartitionsTest, HealthChangedHostsFollowUpdates) {
  const auto& host_set =
      dynamic_cast<const PersistentHostSet&>(*priority_set_.hostSetsPerPriority()[0]);
  PrioritySetImpl worker;
  worker.setHostSetFactory(&PersistentHostSet::create);
  worker.getOrCreateHostSet(0);
  const auto publish = [&](const HostVector& added, const HostVector& removed) {
    worker.updateHosts(
        0,
        HostSetImpl::snapshotUpdateHostsParams(priority_set_.hostSetsPerPriority()[0]->snapshot()),
        nullptr, added, removed, std::nullopt, std::nullopt);
    return &dynamic_cast<const PersistentHostSet&>(*worker.hostSetsPerPriority()[0]);
  };

  members_ = {makeHost(1, "a"), makeHost(2, "b")};
  delta(0, members_, {});
  ASSERT_TRUE(host_set.healthChangedHosts().has_value());
  EXPECT_TRUE(host_set.healthChangedHosts()->empty());
  const PersistentHostSet* worker_set = publish(members_, {});
  ASSERT_TRUE(worker_set->healthChangedHosts().has_value());
  EXPECT_TRUE(worker_set->healthChangedHosts()->empty());

  members_[1]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_TRUE(healthChange(0, members_[1]));
  EXPECT_EQ(HostVector{members_[1]}, host_set.healthChangedHosts());
  worker_set = publish({}, {});
  EXPECT_EQ(HostVector{members_[1]}, worker_set->healthChangedHosts());

  delta(0, {}, {members_[0]});
  EXPECT_TRUE(host_set.healthChangedHosts()->empty());

  priority_set_.updateHosts(0,
                            HostSetImpl::partitionHosts(std::make_shared<HostVector>(members_),
                                                        groupByLocality(members_)),
                            nullptr, {}, {}, std::nullopt, std::nullopt);
  EXPECT_FALSE(host_set.healthChangedHosts().has_value());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
