/* Copyright 2026 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/router/string_accessor.h"

#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/bool_accessor_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/extensions/io_socket/user_space/io_handle_impl.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/host.h"

#include "absl/strings/str_cat.h"
#include "contrib/istio/filters/common/source/peer_metadata_registry.h"
#include "contrib/istio/filters/network/peer_metadata/source/peer_metadata.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PeerMetadata {
namespace {

constexpr absl::string_view defaultBaggageKey = "io.istio.baggage";
constexpr absl::string_view defaultIdentity = "spiffe://cluster.local/ns/default/sa/default";
constexpr absl::string_view defaultBaggage =
    "k8s.deployment.name=server,service.name=server-service,service.version=v2,k8s.namespace.name="
    "default,k8s.cluster.name=cluster";

using ::testing::_;
using ::testing::Const;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

// In-memory PeerMetadataRegistry used by tests to stand in for the thread-local
// registry that hands peer metadata from the listener-side filter to the
// upstream (cluster-side) filter.
class TestPeerMetadataRegistry : public Filters::Common::PeerMetadataShared::PeerMetadataRegistry {
public:
  void setValue(void* key, const std::string& value) override { store_[key] = value; }

  std::optional<std::string> getValue(void* key) const override {
    const auto it = store_.find(key);
    if (it == store_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void removeValue(void* key) override { store_.erase(key); }

private:
  std::map<void*, std::string> store_;
};

// Serializes peer metadata the same way the listener-side filter stores it in
// the registry: a google.protobuf.Struct packed into an Any.
std::string encodePeerMetadata(absl::string_view baggage, absl::string_view identity) {
  std::unique_ptr<Istio::Common::WorkloadMetadataObject> metadata =
      Istio::Common::convertBaggageToWorkloadMetadata(baggage, identity);
  Protobuf::Struct data = ::Istio::Common::convertWorkloadMetadataToStruct(*metadata);
  Protobuf::Any wrapped;
  std::ignore = wrapped.PackFrom(data);
  return wrapped.SerializeAsString();
}

// Decodes a value stored in the registry back into a WorkloadMetadataObject.
std::optional<Istio::Common::WorkloadMetadataObject>
decodePeerMetadata(const std::string& serialized) {
  Protobuf::Any any;
  if (!any.ParseFromString(serialized)) {
    return std::nullopt;
  }
  Protobuf::Struct data;
  if (!any.UnpackTo(&data)) {
    return std::nullopt;
  }
  std::unique_ptr<Istio::Common::WorkloadMetadataObject> metadata =
      Istio::Common::convertStructToWorkloadMetadata(data);
  if (!metadata) {
    return std::nullopt;
  }
  return *metadata;
}

// Wires a real user-space IoHandle into `connection` so the source filter's
// pointer-keyed registry path derives a stable key from the shared
// PassthroughState. The IoHandle and its owning socket are stored in `io_handle`
// and `socket` (which must outlive the connection), and the key the filter will
// compute is returned.
void* wireUserSpaceSocket(NiceMock<Network::MockConnection>& connection,
                          Extensions::IoSocket::UserSpace::IoHandleImplPtr& io_handle,
                          Network::ConnectionSocketPtr& socket) {
  auto pair = Extensions::IoSocket::UserSpace::IoHandleFactory::createIoHandlePair();
  io_handle = std::move(pair.first);
  auto owned_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  ON_CALL(*owned_socket, ioHandle()).WillByDefault(ReturnRef(*io_handle));
  ON_CALL(Const(*owned_socket), ioHandle()).WillByDefault(ReturnRef(*io_handle));
  socket = std::move(owned_socket);
  ON_CALL(connection, getSocket()).WillByDefault(ReturnRef(socket));
  return io_handle->passthroughState().get();
}

// Which hand-off path the parameterized fixtures exercise. The path is selected
// by whether a registry is present: with a registry the shared PassthroughState
// pointer keys the thread-local registry; without one the filters fall back to
// the legacy data-stream preamble exchange.
enum class ExchangePath { DataStreamPreamble, ThreadLocalRegistry };

// Readable parameter names for the path-parameterized fixtures below.
std::string modeName(const ::testing::TestParamInfo<ExchangePath>& info) {
  return info.param == ExchangePath::ThreadLocalRegistry ? "ThreadLocalRegistry"
                                                         : "DataStreamPreamble";
}

class PeerMetadataFilterTest : public ::testing::TestWithParam<ExchangePath> {
public:
  // Builds a downstream Filter config. The exchange path is not selected by
  // config; it is driven by whether the filter is given a registry.
  Config makeConfig(absl::string_view baggage_key) {
    Config config;
    config.set_baggage_key(baggage_key);
    return config;
  }

  void populateNodeMetadata(absl::string_view workload_name, absl::string_view workload_type,
                            absl::string_view service_name, absl::string_view service_version,
                            absl::string_view ns, absl::string_view cluster) {
    auto metadata = node_metadata_.mutable_metadata()->mutable_fields();
    (*metadata)["NAMESPACE"].set_string_value(ns);
    (*metadata)["CLUSTER_ID"].set_string_value(cluster);
    (*metadata)["WORKLOAD_NAME"].set_string_value(workload_name);
    (*metadata)["OWNER"].set_string_value(absl::StrCat("kubernetes://apis/apps/v1/namespaces/", ns,
                                                       "/", workload_type, "s/", workload_name));
    auto labels = (*metadata)["LABELS"].mutable_struct_value()->mutable_fields();
    (*labels)["service.istio.io/canonical-name"].set_string_value(service_name);
    (*labels)["service.istio.io/canonical-revision"].set_string_value(service_version);
  }

  void populateTcpProxyResponseHeaders(const Http::ResponseHeaderMap& response_headers) {
    auto headers = std::make_shared<TcpProxy::TunnelResponseHeaders>(
        std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers));
    auto filter_state = stream_info_.filterState();
    filter_state->setData(TcpProxy::TunnelResponseHeaders::key(), headers,
                          StreamInfo::FilterState::LifeSpan::Connection);
  }

  void populateUpstreamSans(const std::vector<std::string>& sans) {
    sans_ = sans;
    auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
    ON_CALL(*ssl, uriSanPeerCertificate()).WillByDefault(Return(sans_));
    auto upstream = stream_info_.upstreamInfo();
    upstream->setUpstreamSslConnection(ssl);
  }

  void disablePeerDiscovery() {
    Protobuf::Struct metadata;
    auto fields = metadata.mutable_fields();
    (*fields)[FilterNames::get().DisableDiscoveryField].set_bool_value(true);
    (*stream_info_.metadata_.mutable_filter_metadata())[FilterNames::get().Name].MergeFrom(
        metadata);
  }

  void configureThreadLocalExchange() {
    const bool enable = GetParam() == ExchangePath::ThreadLocalRegistry;
    if (enable) {
      stream_info_.filterState()->setData(
          FilterNames::get().EnableTLSFilterExchange,
          std::make_shared<StreamInfo::BoolAccessorImpl>(true),
          StreamInfo::FilterState::LifeSpan::Connection,
          StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    } else {
      stream_info_.filterState()->setData(
          FilterNames::get().EnableTLSFilterExchange,
          std::make_shared<StreamInfo::BoolAccessorImpl>(false),
          StreamInfo::FilterState::LifeSpan::Connection,
          StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    }
  }

  void initialize(const Config& config) {
    const bool use_registry = GetParam() == ExchangePath::ThreadLocalRegistry;

    ON_CALL(local_info_, node()).WillByDefault(ReturnRef(node_metadata_));

    ON_CALL(read_filter_callbacks_.connection_, streamInfo())
        .WillByDefault(ReturnRef(stream_info_));
    ON_CALL(Const(read_filter_callbacks_.connection_), streamInfo())
        .WillByDefault(ReturnRef(stream_info_));
    ON_CALL(write_filter_callbacks_.connection_, streamInfo())
        .WillByDefault(ReturnRef(stream_info_));
    ON_CALL(Const(write_filter_callbacks_.connection_), streamInfo())
        .WillByDefault(ReturnRef(stream_info_));
    ON_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, _))
        .WillByDefault([this](Buffer::Instance& data, bool) { injected_write_data_.add(data); });

    if (use_registry) {
      registry_key_ =
          wireUserSpaceSocket(read_filter_callbacks_.connection_, io_handle_, connection_socket_);
    }

    filter_ = std::make_unique<Filter>(config, local_info_, use_registry ? registry_ : nullptr);
    filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_filter_callbacks_);
  }

  std::optional<Istio::Common::WorkloadMetadataObject> propagatedPeerMetadata() {
    if (GetParam() == ExchangePath::ThreadLocalRegistry) {
      const auto stored = registry_->getValue(registry_key_);
      if (!stored.has_value()) {
        return std::nullopt;
      }
      return decodePeerMetadata(*stored);
    }
    if (injected_write_data_.length() < sizeof(PeerMetadataHeader)) {
      return std::nullopt;
    }
    PeerMetadataHeader header;
    injected_write_data_.copyOut(0, sizeof(header), &header);
    if (header.magic != PeerMetadataHeader::magic_number || header.data_size == 0 ||
        injected_write_data_.length() < sizeof(header) + header.data_size) {
      return std::nullopt;
    }
    std::string serialized;
    serialized.resize(header.data_size);
    injected_write_data_.copyOut(sizeof(header), header.data_size, serialized.data());
    return decodePeerMetadata(serialized);
  }

  ::envoy::config::core::v3::Node node_metadata_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_filter_callbacks_;
  std::vector<std::string> sans_;
  std::shared_ptr<TestPeerMetadataRegistry> registry_ =
      std::make_shared<TestPeerMetadataRegistry>();
  Buffer::OwnedImpl injected_write_data_;
  Extensions::IoSocket::UserSpace::IoHandleImplPtr io_handle_;
  Network::ConnectionSocketPtr connection_socket_;
  void* registry_key_{nullptr};
  std::unique_ptr<Filter> filter_;
};

TEST_P(PeerMetadataFilterTest, TestPopulateBaggage) {
  populateNodeMetadata("workload", "deployment", "workload-service", "v1", "default", "cluster");
  initialize(makeConfig(defaultBaggageKey));
  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  const auto filter_state = stream_info_.filterState();
  EXPECT_TRUE(filter_state->hasDataWithName(defaultBaggageKey));
  const auto* baggage = filter_state->getDataReadOnly<Router::StringAccessor>(defaultBaggageKey);
  EXPECT_EQ(baggage->asString(),
            "k8s.deployment.name=workload,service.name=workload-service,service.version=v1,k8s."
            "namespace.name=default,k8s.cluster.name=cluster");
}

TEST_P(PeerMetadataFilterTest, TestDoNotPopulateBaggage) {
  populateNodeMetadata("workload", "deployment", "workload-service", "v1", "default", "cluster");
  initialize(makeConfig(""));
  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  const auto filter_state = stream_info_.filterState();
  EXPECT_FALSE(
      filter_state->hasDataAtOrAboveLifeSpan(StreamInfo::FilterState::LifeSpan::FilterChain));
}

TEST_P(PeerMetadataFilterTest, TestPeerMetadataDiscoveredAndPropagated) {
  const std::string baggage{defaultBaggage};
  const std::string identity{defaultIdentity};

  populateNodeMetadata("workload", "deployment", "workload-service", "v1", "default", "cluster");
  initialize(makeConfig(defaultBaggageKey));
  configureThreadLocalExchange();

  Http::TestResponseHeaderMapImpl headers{{Headers::get().Baggage.get(), baggage}};
  populateTcpProxyResponseHeaders(headers);

  std::vector<std::string> sans{identity};
  populateUpstreamSans(sans);

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  Buffer::OwnedImpl data;
  EXPECT_EQ(filter_->onWrite(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  const auto workload = propagatedPeerMetadata();
  ASSERT_TRUE(workload.has_value());
  EXPECT_EQ(workload->baggage(), baggage);
  EXPECT_EQ(workload->identity(), identity);
}

TEST_P(PeerMetadataFilterTest, TestNoFiltersStateFromTcpProxy) {
  const std::string identity{defaultIdentity};

  populateNodeMetadata("workload", "deployment", "workload-service", "v1", "default", "cluster");
  initialize(makeConfig(defaultBaggageKey));
  configureThreadLocalExchange();

  std::vector<std::string> sans{identity};
  populateUpstreamSans(sans);

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  Buffer::OwnedImpl data;
  EXPECT_EQ(filter_->onWrite(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  EXPECT_FALSE(propagatedPeerMetadata().has_value());
}

TEST_P(PeerMetadataFilterTest, TestNoBaggageHeaderFromTcpProxy) {
  const std::string baggage{defaultBaggage};
  const std::string identity{defaultIdentity};

  populateNodeMetadata("workload", "deployment", "workload-service", "v1", "default", "cluster");
  initialize(makeConfig(defaultBaggageKey));
  configureThreadLocalExchange();

  Http::TestResponseHeaderMapImpl headers{{"bibbage", baggage}};
  populateTcpProxyResponseHeaders(headers);

  std::vector<std::string> sans{identity};
  populateUpstreamSans(sans);

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  Buffer::OwnedImpl data;
  EXPECT_EQ(filter_->onWrite(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  EXPECT_FALSE(propagatedPeerMetadata().has_value());
}

TEST_P(PeerMetadataFilterTest, TestDisablePeerDiscovery) {
  const std::string baggage{defaultBaggage};
  const std::string identity{defaultIdentity};

  populateNodeMetadata("workload", "deployment", "workload-service", "v1", "default", "cluster");
  initialize(makeConfig(defaultBaggageKey));
  configureThreadLocalExchange();

  Http::TestResponseHeaderMapImpl headers{{Headers::get().Baggage.get(), baggage}};
  populateTcpProxyResponseHeaders(headers);

  std::vector<std::string> sans{identity};
  populateUpstreamSans(sans);

  disablePeerDiscovery();

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  Buffer::OwnedImpl data;
  EXPECT_EQ(filter_->onWrite(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  EXPECT_FALSE(propagatedPeerMetadata().has_value());
}

INSTANTIATE_TEST_SUITE_P(Modes, PeerMetadataFilterTest,
                         ::testing::Values(ExchangePath::DataStreamPreamble,
                                           ExchangePath::ThreadLocalRegistry),
                         modeName);

std::shared_ptr<NiceMock<Upstream::MockHostDescription>>
makeInternalListenerHost(absl::string_view listener_name) {
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  host->address_ =
      std::make_shared<Network::Address::EnvoyInternalInstance>(std::string(listener_name));
  ON_CALL(*host, address()).WillByDefault(Return(host->address_));
  return host;
}

std::shared_ptr<NiceMock<Upstream::MockHostDescription>> makeIpv4Host(absl::string_view address) {
  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  host->address_ = std::make_shared<Network::Address::Ipv4Instance>(std::string(address));
  ON_CALL(*host, address()).WillByDefault(Return(host->address_));
  return host;
}

class PeerMetadataUpstreamFilterTestBase : public ::testing::Test {
public:
  void initialize(bool use_registry = false) {
    ON_CALL(callbacks_.connection_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
    ON_CALL(Const(callbacks_.connection_), streamInfo()).WillByDefault(ReturnRef(stream_info_));

    host_metadata_ = std::make_shared<::envoy::config::core::v3::Metadata>();

    if (use_registry) {
      stream_info_.filterState()->setData(
          FilterNames::get().EnableTLSFilterExchange,
          std::make_shared<StreamInfo::BoolAccessorImpl>(true),
          StreamInfo::FilterState::LifeSpan::Connection,
          StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
      registry_key_ = wireUserSpaceSocket(callbacks_.connection_, io_handle_, connection_socket_);
    } else {
      stream_info_.filterState()->setData(
          FilterNames::get().EnableTLSFilterExchange,
          std::make_shared<StreamInfo::BoolAccessorImpl>(false),
          StreamInfo::FilterState::LifeSpan::Connection,
          StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    }

    filter_ = std::make_unique<UpstreamFilter>(use_registry ? registry_ : nullptr);
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  ::envoy::config::core::v3::Metadata& hostMetadata() { return *host_metadata_; }

  ::envoy::config::core::v3::Metadata& clusterMetadata() {
    return upstream_host_->cluster_.metadata_;
  }

  std::optional<Istio::Common::WorkloadMetadataObject> peerInfoFromFilterState() const {
    const auto& filter_state = stream_info_.filterState();
    const auto* cel_state =
        filter_state.getDataReadOnly<Extensions::Filters::Common::Expr::CelState>(
            Istio::Common::UpstreamPeer);
    if (!cel_state) {
      return std::nullopt;
    }

    Protobuf::Struct obj;
    if (!obj.ParseFromString(absl::string_view(cel_state->value()))) {
      return std::nullopt;
    }

    std::unique_ptr<Istio::Common::WorkloadMetadataObject> peer_info =
        Istio::Common::convertStructToWorkloadMetadata(obj);
    if (!peer_info) {
      return std::nullopt;
    }

    return *peer_info;
  }

  bool noPeerPopulated() const {
    return stream_info_.filterState().hasDataWithName(Istio::Common::NoPeer);
  }

  void setUpstreamHost(const std::shared_ptr<NiceMock<Upstream::MockHostDescription>>& host) {
    upstream_host_ = host;
    ON_CALL(Const(*upstream_host_), metadata()).WillByDefault(Return(host_metadata_));
    stream_info_.upstreamInfo()->setUpstreamHost(upstream_host_);
  }

  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Network::MockReadFilterCallbacks> callbacks_;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> upstream_host_;
  std::shared_ptr<::envoy::config::core::v3::Metadata> host_metadata_;
  std::shared_ptr<TestPeerMetadataRegistry> registry_ =
      std::make_shared<TestPeerMetadataRegistry>();
  Extensions::IoSocket::UserSpace::IoHandleImplPtr io_handle_;
  Network::ConnectionSocketPtr connection_socket_;
  void* registry_key_{nullptr};
  std::unique_ptr<UpstreamFilter> filter_;
};

// Parameterized wrapper that selects the hand-off path (registry vs. legacy
// data-stream preamble) and provides path-aware delivery helpers.
class PeerMetadataUpstreamFilterTest : public PeerMetadataUpstreamFilterTestBase,
                                       public ::testing::WithParamInterface<ExchangePath> {
public:
  void initialize() {
    PeerMetadataUpstreamFilterTestBase::initialize(GetParam() == ExchangePath::ThreadLocalRegistry);
  }

  void deliverPeerMetadata(Buffer::Instance& data, absl::string_view baggage,
                           absl::string_view identity) {
    const std::string serialized = encodePeerMetadata(baggage, identity);
    if (GetParam() == ExchangePath::ThreadLocalRegistry) {
      registry_->setValue(registry_key_, serialized);
      return;
    }
    PeerMetadataHeader header{PeerMetadataHeader::magic_number,
                              static_cast<uint32_t>(serialized.size())};
    Buffer::OwnedImpl preamble{
        absl::string_view(reinterpret_cast<const char*>(&header), sizeof(header))};
    preamble.add(serialized);
    preamble.add(data);
    data.drain(data.length());
    data.add(preamble);
  }
};

TEST_P(PeerMetadataUpstreamFilterTest, TestPeerMetadataConsumedAndPropagated) {
  initialize();
  setUpstreamHost(makeInternalListenerHost("connect_originate"));

  Buffer::OwnedImpl data;
  data.add("application data");
  deliverPeerMetadata(data, defaultBaggage, defaultIdentity);

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);
  EXPECT_EQ(filter_->onData(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  // In legacy mode the preamble is stripped, leaving only the payload; in registry mode the
  // data is untouched. Either way only the payload remains.
  EXPECT_EQ(data.toString(), "application data");
  // The registry entry (if any) is consumed once read.
  EXPECT_FALSE(registry_->getValue(registry_key_).has_value());

  const auto peer_info = peerInfoFromFilterState();
  ASSERT_TRUE(peer_info.has_value());
  EXPECT_EQ(peer_info->identity(), defaultIdentity);
  EXPECT_EQ(peer_info->baggage(), defaultBaggage);
}

TEST_P(PeerMetadataUpstreamFilterTest, TestPeerMetadataNotConsumedForUnknownInternalListeners) {
  initialize();
  setUpstreamHost(makeInternalListenerHost("not_connect_originate"));
  registry_->setValue(registry_key_, encodePeerMetadata(defaultBaggage, defaultIdentity));

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  const std::string payload{"application data"};
  Buffer::OwnedImpl data;
  data.add(payload);
  EXPECT_EQ(filter_->onData(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  // Internal listener is unknown, so no peer metadata is populated.
  const auto peer_info = peerInfoFromFilterState();
  EXPECT_FALSE(peer_info.has_value());
}

TEST_P(PeerMetadataUpstreamFilterTest, TestPeerMetadataNotConsumedForExternalHosts) {
  initialize();
  setUpstreamHost(makeIpv4Host("192.168.0.1"));
  registry_->setValue(registry_key_, encodePeerMetadata(defaultBaggage, defaultIdentity));

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  const std::string payload{"application data"};
  Buffer::OwnedImpl data;
  data.add(payload);
  EXPECT_EQ(filter_->onData(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  EXPECT_EQ(data.length(), payload.size());
  const auto peer_info = peerInfoFromFilterState();
  EXPECT_FALSE(peer_info.has_value());
}

TEST_P(PeerMetadataUpstreamFilterTest, TestPeerMetadataNotConsumedWhenDisabledViaHostMetadata) {
  initialize();
  setUpstreamHost(makeInternalListenerHost("connect_originate"));

  Protobuf::Struct metadata; // NOLINT(google.protobuf)
  auto fields = metadata.mutable_fields();
  (*fields)[FilterNames::get().DisableDiscoveryField].set_bool_value(true);
  (*hostMetadata().mutable_filter_metadata())[FilterNames::get().Name].MergeFrom(metadata);

  registry_->setValue(registry_key_, encodePeerMetadata(defaultBaggage, defaultIdentity));

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  const std::string payload{"application data"};
  Buffer::OwnedImpl data;
  data.add(payload);
  EXPECT_EQ(filter_->onData(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  EXPECT_TRUE(registry_->getValue(registry_key_).has_value());
  const auto peer_info = peerInfoFromFilterState();
  EXPECT_FALSE(peer_info.has_value());
}

TEST_P(PeerMetadataUpstreamFilterTest, TestPeerMetadataNotConsumedWhenDisabledViaClusterMetadata) {
  initialize();
  setUpstreamHost(makeInternalListenerHost("connect_originate"));

  Protobuf::Struct metadata2;
  auto fields2 = metadata2.mutable_fields();
  (*fields2)[FilterNames::get().DisableDiscoveryField].set_bool_value(true);
  (*clusterMetadata().mutable_filter_metadata())[FilterNames::get().Name].MergeFrom(metadata2);

  registry_->setValue(registry_key_, encodePeerMetadata(defaultBaggage, defaultIdentity));

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  const std::string payload{"application data"};
  Buffer::OwnedImpl data;
  data.add(payload);
  EXPECT_EQ(filter_->onData(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  EXPECT_TRUE(registry_->getValue(registry_key_).has_value());
  const auto peer_info = peerInfoFromFilterState();
  EXPECT_FALSE(peer_info.has_value());
}

// Registry hand-off is active (the pointer key is enabled) but nothing was ever
// stored under that key. The upstream filter must fall back to marking the peer
// as unknown by populating NoPeer.
TEST_F(PeerMetadataUpstreamFilterTestBase, TestNoPeerPopulatedWhenRegistryEmpty) {
  initialize(/*use_registry=*/true);
  setUpstreamHost(makeInternalListenerHost("connect_originate"));

  EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);

  const std::string payload{"application data"};
  Buffer::OwnedImpl data;
  data.add(payload);
  EXPECT_EQ(filter_->onData(data, /*end_stream*/ false), Network::FilterStatus::Continue);

  EXPECT_TRUE(noPeerPopulated());
  EXPECT_FALSE(peerInfoFromFilterState().has_value());
  // The application data must be left untouched.
  EXPECT_EQ(data.toString(), payload);
}

INSTANTIATE_TEST_SUITE_P(Modes, PeerMetadataUpstreamFilterTest,
                         ::testing::Values(ExchangePath::DataStreamPreamble,
                                           ExchangePath::ThreadLocalRegistry),
                         modeName);

} // namespace
} // namespace PeerMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
