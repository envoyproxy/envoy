package main

import (
	"context"
	"log"
	"net"
	"sync"
	"time"

	discoverygrpc "github.com/envoyproxy/go-control-plane/envoy/service/discovery/v3"
	"github.com/envoyproxy/go-control-plane/pkg/cache/types"
	"github.com/envoyproxy/go-control-plane/pkg/cache/v3"
	"github.com/envoyproxy/go-control-plane/pkg/resource/v3"
	"google.golang.org/grpc"
	"google.golang.org/grpc/keepalive"

	cluster "github.com/envoyproxy/go-control-plane/envoy/config/cluster/v3"
	core "github.com/envoyproxy/go-control-plane/envoy/config/core/v3"
	endpoint "github.com/envoyproxy/go-control-plane/envoy/config/endpoint/v3"
	listener "github.com/envoyproxy/go-control-plane/envoy/config/listener/v3"
	route "github.com/envoyproxy/go-control-plane/envoy/config/route/v3"
	router "github.com/envoyproxy/go-control-plane/envoy/extensions/filters/http/router/v3"
	hcm "github.com/envoyproxy/go-control-plane/envoy/extensions/filters/network/http_connection_manager/v3"
	"google.golang.org/protobuf/types/known/anypb"
	"google.golang.org/protobuf/types/known/durationpb"
)

const (
	ClusterName  = "example_proxy_cluster"
	RouteName    = "local_route"
	ListenerName = "listener_0"
	ListenerPort = 8080
	UpstreamHost = "www.envoyproxy.io"
	UpstreamPort = 443
)

type server struct {
	cache cache.SnapshotCache
}

// Stream is a bidirectional stream for ADS
type Stream interface {
	Send(*discoverygrpc.DiscoveryResponse) error
	Recv() (*discoverygrpc.DiscoveryRequest, error)
}

// Callbacks implements the server callbacks
type Callbacks struct {
	mu        sync.Mutex
	fetches   int
	requests  int
	responses int
}

func (cb *Callbacks) Report() {
	cb.mu.Lock()
	defer cb.mu.Unlock()
	log.Printf("📊 Server stats: fetches=%d requests=%d responses=%d", cb.fetches, cb.requests, cb.responses)
}

func (cb *Callbacks) OnStreamOpen(ctx context.Context, id int64, typ string) error {
	log.Printf("🔗 OnStreamOpen %d open for %s", id, typ)
	return nil
}

func (cb *Callbacks) OnStreamClosed(id int64) {
	log.Printf("🔗 OnStreamClosed %d closed", id)
}

func (cb *Callbacks) OnStreamRequest(id int64, req *discoverygrpc.DiscoveryRequest) error {
	cb.mu.Lock()
	defer cb.mu.Unlock()
	cb.requests++
	log.Printf("📨 OnStreamRequest[%d]: %s (version=%s, nonce=%s)",
		id, req.GetTypeUrl(), req.GetVersionInfo(), req.GetResponseNonce())

	if len(req.GetResourceNames()) > 0 {
		log.Printf("   Resources requested: %v", req.GetResourceNames())
	}

	return nil
}

func (cb *Callbacks) OnStreamResponse(ctx context.Context, id int64, req *discoverygrpc.DiscoveryRequest, resp *discoverygrpc.DiscoveryResponse) {
	cb.mu.Lock()
	defer cb.mu.Unlock()
	cb.responses++
	log.Printf("📤 OnStreamResponse[%d]: %s (version=%s, nonce=%s, resources=%d)",
		id, resp.GetTypeUrl(), resp.GetVersionInfo(), resp.GetNonce(), len(resp.GetResources()))
}

func (cb *Callbacks) OnFetchRequest(ctx context.Context, req *discoverygrpc.DiscoveryRequest) error {
	cb.mu.Lock()
	defer cb.mu.Unlock()
	cb.fetches++
	log.Printf("🔍 OnFetchRequest: %s", req.GetTypeUrl())
	return nil
}

func (cb *Callbacks) OnFetchResponse(req *discoverygrpc.DiscoveryRequest, resp *discoverygrpc.DiscoveryResponse) {
	log.Printf("🔍 OnFetchResponse: %s", resp.GetTypeUrl())
}

func makeCluster(clusterName string) *cluster.Cluster {
	return &cluster.Cluster{
		Name:                 clusterName,
		ConnectTimeout:       durationpb.New(5 * time.Second),
		ClusterDiscoveryType: &cluster.Cluster_Type{Type: cluster.Cluster_LOGICAL_DNS},
		LbPolicy:             cluster.Cluster_ROUND_ROBIN,
		LoadAssignment:       makeEndpoint(clusterName),
		DnsLookupFamily:      cluster.Cluster_V4_ONLY,
	}
}

func makeEndpoint(clusterName string) *endpoint.ClusterLoadAssignment {
	return &endpoint.ClusterLoadAssignment{
		ClusterName: clusterName,
		Endpoints: []*endpoint.LocalityLbEndpoints{{
			LbEndpoints: []*endpoint.LbEndpoint{{
				HostIdentifier: &endpoint.LbEndpoint_Endpoint{
					Endpoint: &endpoint.Endpoint{
						Address: &core.Address{
							Address: &core.Address_SocketAddress{
								SocketAddress: &core.SocketAddress{
									Protocol: core.SocketAddress_TCP,
									Address:  UpstreamHost,
									PortSpecifier: &core.SocketAddress_PortValue{
										PortValue: UpstreamPort,
									},
								},
							},
						},
					},
				},
			}},
		}},
	}
}

func makeRoute(routeName string, clusterName string) *route.RouteConfiguration {
	return &route.RouteConfiguration{
		Name: routeName,
		VirtualHosts: []*route.VirtualHost{{
			Name:    "local_service",
			Domains: []string{"*"},
			Routes: []*route.Route{{
				Match: &route.RouteMatch{
					PathSpecifier: &route.RouteMatch_Prefix{
						Prefix: "/",
					},
				},
				Action: &route.Route_Route{
					Route: &route.RouteAction{
						ClusterSpecifier: &route.RouteAction_Cluster{
							Cluster: clusterName,
						},
						HostRewriteSpecifier: &route.RouteAction_HostRewriteLiteral{
							HostRewriteLiteral: UpstreamHost,
						},
					},
				},
			}},
		}},
	}
}

func makeHTTPListener(listenerName string, routeName string) *listener.Listener {
	// HTTP filter configuration
	router := &router.Router{}
	routerAny, _ := anypb.New(router)

	// HTTP connection manager configuration
	manager := &hcm.HttpConnectionManager{
		CodecType:  hcm.HttpConnectionManager_AUTO,
		StatPrefix: "http",
		RouteSpecifier: &hcm.HttpConnectionManager_Rds{
			Rds: &hcm.Rds{
				ConfigSource:    makeConfigSource(),
				RouteConfigName: routeName,
			},
		},
		HttpFilters: []*hcm.HttpFilter{{
			Name:       "envoy.filters.http.router",
			ConfigType: &hcm.HttpFilter_TypedConfig{TypedConfig: routerAny},
		}},
	}
	managerAny, _ := anypb.New(manager)

	return &listener.Listener{
		Name: listenerName,
		Address: &core.Address{
			Address: &core.Address_SocketAddress{
				SocketAddress: &core.SocketAddress{
					Protocol: core.SocketAddress_TCP,
					Address:  "0.0.0.0",
					PortSpecifier: &core.SocketAddress_PortValue{
						PortValue: ListenerPort,
					},
				},
			},
		},
		FilterChains: []*listener.FilterChain{{
			Filters: []*listener.Filter{{
				Name: "envoy.filters.network.http_connection_manager",
				ConfigType: &listener.Filter_TypedConfig{
					TypedConfig: managerAny,
				},
			}},
		}},
	}
}

func makeConfigSource() *core.ConfigSource {
	return &core.ConfigSource{
		ResourceApiVersion: core.ApiVersion_V3,
		ConfigSourceSpecifier: &core.ConfigSource_Ads{
			Ads: &core.AggregatedConfigSource{},
		},
	}
}

func GenerateSnapshot() *cache.Snapshot {
	snap, _ := cache.NewSnapshot("1",
		map[resource.Type][]types.Resource{
			resource.ClusterType:  {makeCluster(ClusterName)},
			resource.RouteType:    {makeRoute(RouteName, ClusterName)},
			resource.ListenerType: {makeHTTPListener(ListenerName, RouteName)},
		},
	)
	return snap
}

type BidirectionalXDSServer struct {
	server.Server
	callbacks *Callbacks
}

// StreamAggregatedResources implements the bidirectional ADS
func (s *BidirectionalXDSServer) StreamAggregatedResources(stream discoverygrpc.AggregatedDiscoveryService_StreamAggregatedResourcesServer) error {
	log.Println("🚀 New bidirectional ADS stream established")

	// Handle the stream using the control plane server
	return s.Server.StreamAggregatedResources(stream)
}

func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)

	log.Println("🚀 Starting Bidirectional xDS Management Server")
	log.Println("==============================================")

	// Create a cache
	cache := cache.NewSnapshotCache(false, cache.IDHash{}, nil)

	// Create callbacks
	callbacks := &Callbacks{}

	// Create the xDS server
	srv := server.NewServer(context.Background(), cache, callbacks)

	// Wrap with bidirectional support
	bidirectionalServer := &BidirectionalXDSServer{
		Server:    srv,
		callbacks: callbacks,
	}

	// Create initial snapshot
	nodeId := "test-id"
	snapshot := GenerateSnapshot()
	if err := cache.SetSnapshot(context.Background(), nodeId, snapshot); err != nil {
		log.Fatalf("❌ Failed to set snapshot: %v", err)
	}
	log.Printf("✅ Initial snapshot set for node: %s", nodeId)

	// Start gRPC server
	lis, err := net.Listen("tcp", ":18000")
	if err != nil {
		log.Fatalf("❌ Failed to listen: %v", err)
	}

	grpcServer := grpc.NewServer(
		grpc.KeepaliveParams(keepalive.ServerParameters{
			Time:    30 * time.Second,
			Timeout: 5 * time.Second,
		}),
		grpc.KeepaliveEnforcementPolicy(keepalive.EnforcementPolicy{
			MinTime:             30 * time.Second,
			PermitWithoutStream: true,
		}),
	)

	discoverygrpc.RegisterAggregatedDiscoveryServiceServer(grpcServer, bidirectionalServer)

	log.Printf("🌐 Management server listening on :18000")
	log.Printf("📋 Ready to serve:")
	log.Printf("   • Cluster: %s", ClusterName)
	log.Printf("   • Route: %s", RouteName)
	log.Printf("   • Listener: %s (port %d)", ListenerName, ListenerPort)
	log.Printf("   • Upstream: %s:%d", UpstreamHost, UpstreamPort)

	// Start stats reporting
	go func() {
		for {
			time.Sleep(10 * time.Second)
			callbacks.Report()
		}
	}()

	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("❌ Failed to serve: %v", err)
	}
}
