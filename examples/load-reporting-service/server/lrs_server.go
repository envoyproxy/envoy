package server

import (
	"context"
	"github.com/golang/protobuf/ptypes/duration"
	"google.golang.org/grpc"
	"log"
	"sync/atomic"

	core "github.com/envoyproxy/go-control-plane/envoy/api/v2/core"
	gcpLoadStats "github.com/envoyproxy/go-control-plane/envoy/service/load_stats/v2"
)

type NodeMetadata struct {
	stream stream
	node   *core.Node
}

type Server interface {
	gcpLoadStats.LoadReportingServiceServer
	SendResponse(cluster string, upstreamCluster []string, frequency int64)
}

// Callbacks is a collection of callbacks inserted into the server operation.
// The callbacks are invoked synchronously.
type Callbacks interface {
	// OnStreamOpen is called once an xDS stream is open with a stream ID and the type URL (or "" for ADS).
	// Returning an error will end processing and close the stream. OnStreamClosed will still be called.
	OnStreamOpen(context.Context, int64) error
	// OnStreamClosed is called immediately prior to closing an xDS stream with a stream ID.
	OnStreamClosed(int64)
	// OnStreamRequest is called once a request is received on a stream.
	// Returning an error will end processing and close the stream. OnStreamClosed will still be called.
	OnStreamRequest(int64, *gcpLoadStats.LoadStatsRequest) error
	// OnStreamResponse is called immediately prior to sending a response on a stream.
	OnStreamResponse(int64, *gcpLoadStats.LoadStatsResponse)
}

// NewLrsServer creates handlers from a config watcher and callbacks.
func NewServer(ctx context.Context, callbacks Callbacks) Server {
	return &server{callbacks: callbacks, ctx: ctx, lrsCache: make(map[string]NodeMetadata)}
}

type server struct {
	callbacks Callbacks
	// streamCount for counting bi-di streams
	streamCount int64
	ctx         context.Context
	lrsCache    map[string]NodeMetadata
}

type stream interface {
	grpc.ServerStream

	Send(*gcpLoadStats.LoadStatsResponse) error
	Recv() (*gcpLoadStats.LoadStatsRequest, error)
}

// process handles a bi-di stream request
func (s *server) process(stream stream, reqCh <-chan *gcpLoadStats.LoadStatsRequest) error {
	// increment stream count
	streamID := atomic.AddInt64(&s.streamCount, 1)

	if s.callbacks != nil {
		if err := s.callbacks.OnStreamOpen(stream.Context(), streamID); err != nil {
			return err
		}
	}

	for {
		select {
		case <-s.ctx.Done():
			log.Print("Connection ended")
			return nil
		case req, more := <-reqCh:
			// input stream ended or errored out
			if !more {
				return nil
			}

			if s.callbacks != nil {
                if err := s.callbacks.OnStreamRequest(streamID, req); err != nil {
                    return err
                }
            }

			clusterName := req.GetNode().GetCluster()
			if _, exist := s.lrsCache[clusterName]; !exist {
				log.Printf("Adding new cluster to cache `%s`", clusterName)
				s.lrsCache[clusterName] = NodeMetadata{
					node:   req.GetNode(),
					stream: stream,
				}
			} else {
				for i := 0; i < len(req.ClusterStats); i++ {
					if len(req.ClusterStats[i].UpstreamLocalityStats) > 0 {
						log.Printf("Got stats from cluster `%s` - %s", req.Node.Cluster, req.GetClusterStats()[i])
					}
				}
			}
		}
	}
}

// handler converts a blocking read call to channels and initiates stream processing
func (s *server) handler(stream stream) error {
	// a channel for receiving incoming requests
	reqCh := make(chan *gcpLoadStats.LoadStatsRequest)
	reqStop := int32(0)
	go func() {
		for {
			req, err := stream.Recv()
			if atomic.LoadInt32(&reqStop) != 0 {
				return
			}
			if err != nil {
				close(reqCh)
				return
			}
			reqCh <- req
		}
	}()

	err := s.process(stream, reqCh)
	atomic.StoreInt32(&reqStop, 1)

	return err
}

func (s *server) StreamLoadStats(stream gcpLoadStats.LoadReportingService_StreamLoadStatsServer) error {
	return s.handler(stream)
}

func (s *server) SendResponse(cluster string, upstreamClusters []string, frequency int64) {
	clusterDetails, exist := s.lrsCache[cluster]
	if !exist {
		log.Printf("Cannot send response as cluster `%s` is not connected", cluster)
		return
	}

	log.Printf("Creating LRS response with frequency - %d secs", frequency)
	err := clusterDetails.stream.Send(&gcpLoadStats.LoadStatsResponse{
		Clusters:                  upstreamClusters,
		LoadReportingInterval:     &duration.Duration{Seconds: frequency},
		ReportEndpointGranularity: true,
	})

	if err != nil {
		log.Panicf("Unable to send response to cluster %s due to err: %s", clusterDetails.node.Cluster, err)
	}
}
