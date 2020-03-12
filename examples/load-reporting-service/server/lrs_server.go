package server

import (
	"log"
	"sync"

	gcpLoadStats "github.com/envoyproxy/go-control-plane/envoy/service/load_stats/v2"
	"github.com/golang/protobuf/ptypes/duration"
)

// This is how often Envoy will send the load report
const StatsFrequencyInSeconds = 2

// Server handling Load Stats communication
type Server interface {
	gcpLoadStats.LoadReportingServiceServer
	HandleRequest(stream gcpLoadStats.LoadReportingService_StreamLoadStatsServer, request *gcpLoadStats.LoadStatsRequest)
}

func NewServer() Server {
	return &server{nodesConnected: make(map[string]bool)}
}

type server struct {
	// protects nodesConnected
	mu sync.Mutex

	// This cache stores nodes connected to the LRS server
	nodesConnected map[string]bool
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
	nodeID := request.GetNode().GetId()

	s.mu.Lock()
	defer s.mu.Unlock()

	// Check whether any Node has already connected or not.
	// If not, add the NodeID to nodesConnected and enable Load Report with given frequency
	// If yes, log stats
	if _, exist := s.nodesConnected[nodeID]; !exist {
		// Add NodeID to the nodesConnected
		log.Printf("Adding new new node to cache `%s`", nodeID)
		s.nodesConnected[nodeID] = true

		// Initialize Load Reporting
		err := stream.Send(&gcpLoadStats.LoadStatsResponse{
			Clusters:                  []string{"local_service"},
			LoadReportingInterval:     &duration.Duration{Seconds: StatsFrequencyInSeconds},
			ReportEndpointGranularity: true,
		})
		if err != nil {
			log.Panicf("Unable to send response to node %s due to err: %s", nodeID, err)
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
