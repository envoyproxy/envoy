package main

import (
	"context"
	"github.com/envoyproxy/envoy/examples/load_reporting_service/server"
	"golang.org/x/sync/errgroup"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"
	"log"
	"net"
	"time"

	gcpLoadStats "github.com/envoyproxy/go-control-plane/envoy/service/load_stats/v2"
)

func main() {
	var g errgroup.Group

	// Start gRPC server
	g.Go(func() error {
		address := "127.0.0.1:18000"
		lis, err := net.Listen("tcp", address)
		if err != nil {
			panic(err)
		}

		grpcServer := grpc.NewServer()
		ctx := context.Background()

		xdsServer := server.NewServer(ctx, &callbacks{})
		gcpLoadStats.RegisterLoadReportingServiceServer(grpcServer, xdsServer)
		startCollectingStats(xdsServer, "http_service", []string{"local_service"}, 7)

		log.Printf("LRS Server is up and running on %s", address)
		reflection.Register(grpcServer)
		return grpcServer.Serve(lis)
	})

	if err := g.Wait(); err != nil {
		panic(err)
	}
}

func startCollectingStats(server server.Server, cluster string, upstreamClusters []string, frequency int64) {
	ticker := time.NewTicker(time.Duration(10) * time.Second)
	counter := 0
	go func() {
		for range ticker.C {
			if counter == 2 {
				return
			}
			server.SendResponse(cluster, upstreamClusters, frequency)
			counter++
		}
	}()
}

type callbacks struct {
}

func (c *callbacks) OnStreamOpen(ctx context.Context, streamID int64) error {
	log.Printf("Stream open from cluster %s", ctx)
	return nil
}

func (c *callbacks) OnStreamClosed(streamID int64) {
}

func (c *callbacks) OnStreamRequest(streamID int64, request *gcpLoadStats.LoadStatsRequest) error {
	return nil
}

func (c *callbacks) OnStreamResponse(streamID int64, response *gcpLoadStats.LoadStatsResponse) {
}
