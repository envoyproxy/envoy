package main

import (
	"flag"
	"fmt"
	"log"
	"net"

	"github.com/envoy/examples/grpc-s2s/service"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	healthz "google.golang.org/grpc/health"
	healthsvc "google.golang.org/grpc/health/grpc_health_v1"
	"google.golang.org/grpc/reflection"
)

type worldService struct {
	service.UnimplementedWorldServer
}

func (s *worldService) Greet(
	ctx context.Context,
	in *service.WorldRequest,
) (*service.WorldResponse, error) {
	log.Println("World: Received request")
	// TODO call world service here
	return &service.WorldResponse{Reply: "world"}, nil
}

func updateServiceHealth(
	h *healthz.Server,
	service string,
	status healthsvc.HealthCheckResponse_ServingStatus,
) {
	h.SetServingStatus(
		service,
		status,
	)
}

func main() {
	port := flag.Int("port", 8082 "grpc port")

	flag.Parse()

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *port))
	if err != nil {
		log.Fatalf("failed to listen: %v", err)
	}
	gs := grpc.NewServer()
	h := worldService{}
	service.RegisterWorldServer(gs, &h)
	reflection.Register(gs)

	healthServer := healthz.NewServer()
	healthsvc.RegisterHealthServer(gs, healthServer)
	updateServiceHealth(
		healthServer,
		service.World_ServiceDesc.ServiceName,
		healthsvc.HealthCheckResponse_SERVING,
	)

	log.Printf("starting grpc on :%d\n", *port)
	gs.Serve(lis)
}
