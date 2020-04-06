package main

import (
	"log"
	"net"

	"github.com/envoyproxy/envoy/examples/load-reporting-service/server"
	gcpLoadStats "github.com/envoyproxy/go-control-plane/envoy/service/load_stats/v2"
	"google.golang.org/grpc"
)

func main() {
	// Listening on port 18000
	address := ":18000"
	lis, err := net.Listen("tcp", address)
	if err != nil {
		panic(err)
	}

	grpcServer := grpc.NewServer()
	xdsServer := server.NewServer()
	gcpLoadStats.RegisterLoadReportingServiceServer(grpcServer, xdsServer)

	log.Printf("LRS Server is up and running on %s", address)
	err = grpcServer.Serve(lis)
	if err != nil {
		panic(err)
	}
}
