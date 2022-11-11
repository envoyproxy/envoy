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

var worldClient service.WorldClient

type helloService struct {
	service.UnimplementedHelloServer
}

func (s *helloService) Greet(
	ctx context.Context,
	in *service.HelloRequest,
) (*service.HelloResponse, error) {
	log.Println("Hello: Received request")
	req := service.WorldRequest{}
	worldResponse, err := worldClient.Greet(
		ctx,
		&req,
		grpc.WaitForReady(true),
	)
	if err != nil {
		return nil, err
	}

	return &service.HelloResponse{Reply: fmt.Sprintf("hello %s", worldResponse.Reply)}, nil
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

func getWorldServiceClient(conn *grpc.ClientConn) service.WorldClient {
	return service.NewWorldClient(conn)
}

func setupGrpcConn(addr string) (*grpc.ClientConn, error) {
	return grpc.DialContext(
		context.Background(),
		addr,
		grpc.WithInsecure(),
	)
}

func main() {
	port := flag.Int("port", 8081, "grpc port")
	worldServiceAddr := flag.String("world-addr", "", "address of world service")
	flag.Parse()

	if len(*worldServiceAddr) == 0 {
		log.Fatal("Specify world-addr for world service address")
	}

	grpcConn, err := setupGrpcConn(*worldServiceAddr)
	if err != nil {
		log.Fatal(err)
	}
	worldClient = getWorldServiceClient(grpcConn)

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *port))
	if err != nil {
		log.Fatalf("failed to listen: %v", err)
	}
	gs := grpc.NewServer()

	h := helloService{}
	service.RegisterHelloServer(gs, &h)
	reflection.Register(gs)

	healthServer := healthz.NewServer()
	healthsvc.RegisterHealthServer(gs, healthServer)
	updateServiceHealth(
		healthServer,
		service.Hello_ServiceDesc.ServiceName,
		//healthsvc.HealthCheckResponse_SERVING,
		healthsvc.HealthCheckResponse_NOT_SERVING,
	)
	log.Printf("starting grpc on :%d\n", *port)
	gs.Serve(lis)
}
