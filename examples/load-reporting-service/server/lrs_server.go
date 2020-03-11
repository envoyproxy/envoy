package server

import (
	"log"
	"sync"

	core "github.com/envoyproxy/go-control-plane/envoy/api/v2/core"
	gcpLoadStats "github.com/envoyproxy/go-control-plane/envoy/service/load_stats/v2"
	"github.com/golang/protobuf/ptypes/duration"
	"google.golang.org/grpc"
)

// Server handling Load Stats communication
type Server interface {
	gcpLoadStats.LoadReportingServiceServer
	SendResponse(cluster string, upstreamCluster []string, frequency int64)
	HandleRequest(stream gcpLoadStats.LoadReportingService_StreamLoadStatsServer, request *gcpLoadStats.LoadStatsRequest)
}

func NewServer() Server {
	return &server{lrsCache: make(map[string]map[string]NodeMetadata)}
}

type server struct {
	// protects lrsCache
	mu sync.Mutex

	// This cache stores stream objects (and Node data) for every node (within a cluster) upon connection
	lrsCache map[string]map[string]NodeMetadata
}

// Struct to hold stream object and node details
type NodeMetadata struct {
	stream stream
	node   *core.Node
}

type stream interface {
	grpc.ServerStream

	Send(*gcpLoadStats.LoadStatsResponse) error
	Recv() (*gcpLoadStats.LoadStatsRequest, error)
}

// Handles incoming stream connections and LoadStatsRequests
func (s *server) StreamLoadStats(stream gcpLoadStats.LoadReportingService_StreamLoadStatsServer) error {
	for {
		req, err := stream.Recv()
		// input stream ended or errored out
		if err != nil {
			return err
		}

		s.HandleRequest(stream, req)
	}
}

func (s *server) HandleRequest(stream gcpLoadStats.LoadReportingService_StreamLoadStatsServer, request *gcpLoadStats.LoadStatsRequest) {
	clusterName := request.GetNode().GetCluster()
	nodeID := request.GetNode().GetId()

	s.mu.Lock()
	defer s.mu.Unlock()

	// Check whether any Node from Cluster has already connected or not.
	// If yes, Cluster should be present in the cache
	// If not, add Cluster <-> Node <-> Stream object in cache
	if _, exist := s.lrsCache[clusterName]; !exist {
		// Add all Nodes and its stream objects into the cache
		log.Printf("Adding new cluster to cache `%s` with node `%s`", clusterName, nodeID)
		s.lrsCache[clusterName] = make(map[string]NodeMetadata)
		s.lrsCache[clusterName][nodeID] = NodeMetadata{
			node:   request.GetNode(),
			stream: stream,
		}
		return
	}

	if _, exist := s.lrsCache[clusterName][nodeID]; !exist {
		// Add remaining Nodes of a Cluster and its stream objects into the cache
		log.Printf("Adding new node `%s` to existing cluster `%s`", nodeID, clusterName)
		s.lrsCache[clusterName][nodeID] = NodeMetadata{
			node:   request.GetNode(),
			stream: stream,
		}
		return
	}

	// After Load Report is enabled, log the Load Report stats received
	for _, clusterStats := range request.ClusterStats {
		if len(clusterStats.UpstreamLocalityStats) > 0 {
			log.Printf("Got stats from cluster `%s` node `%s` - %s", request.Node.Cluster, request.Node.Id, clusterStats)
		}
	}
}

// Initialize Load Reporting for a given cluster to a list of UpStreamClusters
func (s *server) SendResponse(cluster string, upstreamClusters []string, frequency int64) {
	s.mu.Lock()
	// Check whether any Node from given Cluster is connected or not.
	clusterDetails, exist := s.lrsCache[cluster]
	s.mu.Unlock()
	if !exist {
		log.Printf("Cannot send response as cluster `%s` because is not connected", cluster)
		return
	}

	// To enable Load Report, send LoadStatsResponse to all Nodes within a Cluster
	for nodeID, nodeDetails := range clusterDetails {
		log.Printf("Creating LRS response for cluster %s, node %s with frequency %d secs", nodeDetails.node.Cluster, nodeID, frequency)
		err := nodeDetails.stream.Send(&gcpLoadStats.LoadStatsResponse{
			Clusters:                  upstreamClusters,
			LoadReportingInterval:     &duration.Duration{Seconds: frequency},
			ReportEndpointGranularity: true,
		})

		if err != nil {
			log.Panicf("Unable to send response to cluster %s node %s due to err: %s", nodeDetails.node.Cluster, nodeID, err)
		}
	}
}
