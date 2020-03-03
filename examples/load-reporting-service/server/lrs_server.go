package server

import (
	core "github.com/envoyproxy/go-control-plane/envoy/api/v2/core"
	gcpLoadStats "github.com/envoyproxy/go-control-plane/envoy/service/load_stats/v2"
	"github.com/golang/protobuf/ptypes/duration"
	"google.golang.org/grpc"
	"log"
	"sync"
)

// Server handling Load Stats communication
type Server interface {
	gcpLoadStats.LoadReportingServiceServer
	SendResponse(cluster string, upstreamCluster []string, frequency int64)
}

func NewServer() Server {
	return &server{lrsCache: make(map[string]map[string]NodeMetadata)}
}

type server struct {
	// protects lrsCache
	mu sync.RWMutex

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

		clusterName := req.GetNode().GetCluster()
		nodeId := req.GetNode().GetId()

		s.mu.Lock()
		// Check whether any Node from Cluster has already connected or not.
		// If yes, Cluster should be present in the cache
		// If not, add Cluster <-> Node <-> Stream object in cache
		if _, exist := s.lrsCache[clusterName]; !exist {
			// Add all Nodes and its stream objects into the cache
			log.Printf("Adding new cluster to cache `%s` with node `%s`", clusterName, nodeId)
			s.lrsCache[clusterName] = make(map[string]NodeMetadata)
			s.lrsCache[clusterName][nodeId] = NodeMetadata{
				node:   req.GetNode(),
				stream: stream,
			}
		} else if _, exist := s.lrsCache[clusterName][nodeId]; !exist {
			// Add remaining Nodes of a Cluster and its stream objects into the cache
			log.Printf("Adding new node `%s` to existing cluster `%s`", nodeId, clusterName)
			s.lrsCache[clusterName][nodeId] = NodeMetadata{
				node:   req.GetNode(),
				stream: stream,
			}
		} else {
			// After Load Report is enabled, log the Load Report stats received
			for i := 0; i < len(req.ClusterStats); i++ {
				if len(req.ClusterStats[i].UpstreamLocalityStats) > 0 {
					log.Printf("Got stats from cluster `%s` node `%s` - %s", req.Node.Cluster, req.Node.Id, req.GetClusterStats()[i])
				}
			}
		}
		s.mu.Unlock()
	}
}

// Initialize Load Reporting for a given cluster to a list of UpStreamClusters
func (s *server) SendResponse(cluster string, upstreamClusters []string, frequency int64) {
	s.mu.Lock()
	// Check whether any Node from given Cluster is connected or not.
	clusterDetails, exist := s.lrsCache[cluster]
	if !exist {
		log.Printf("Cannot send response as cluster `%s` because is not connected", cluster)
		return
	}

	// To enable Load Report, send LoadStatsResponse to all Nodes within a Cluster
	for nodeId, nodeDetails := range clusterDetails {
		log.Printf("Creating LRS response for cluster %s, node %s with frequency %d secs", nodeDetails.node.Cluster, nodeId, frequency)
		err := nodeDetails.stream.Send(&gcpLoadStats.LoadStatsResponse{
			Clusters:                  upstreamClusters,
			LoadReportingInterval:     &duration.Duration{Seconds: frequency},
			ReportEndpointGranularity: true,
		})

		if err != nil {
			log.Panicf("Unable to send response to cluster %s node %s due to err: %s", nodeDetails.node.Cluster, nodeId, err)
		}
	}
	s.mu.Unlock()
}
