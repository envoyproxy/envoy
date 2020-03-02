package main

import (
	"context"
	"log"
	"net"
	"time"

	"github.com/envoyproxy/envoy/examples/load_reporting_service/server"
	"golang.org/x/sync/errgroup"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

	gcpLoadStats "github.com/envoyproxy/go-control-plane/envoy/service/load_stats/v2"
)

func main() {
	var g errgroup.Group

	// This is how often Envoy will send the load report
	const StatsFrequencyInSeconds = 2

	// Start gRPC server
	g.Go(func() error {
		// Listening on port 18000
		address := ":18000"
		lis, err := net.Listen("tcp", address)
		if err != nil {
			panic(err)
		}

		grpcServer := grpc.NewServer()
		ctx := context.Background()
		xdsServer := server.NewServer(ctx)
		gcpLoadStats.RegisterLoadReportingServiceServer(grpcServer, xdsServer)
		startCollectingStats(xdsServer, "http_service", []string{"local_service"}, StatsFrequencyInSeconds)

		log.Printf("LRS Server is up and running on %s", address)
		reflection.Register(grpcServer)
		return grpcServer.Serve(lis)
	})

	if err := g.Wait(); err != nil {
		panic(err)
	}
}

func startCollectingStats(server server.Server, cluster string, upstreamClusters []string, frequency int64) {
	// Send LoadStatsResponse after 10 seconds to initiate the Load Reporting
	ticker := time.NewTicker(time.Duration(10) * time.Second)
	go func() {
		for range ticker.C {
			server.SendResponse(cluster, upstreamClusters, frequency)
			return
		}
	}()
}
