package main

import (
	"flag"
	"fmt"
	"log"
	"net"

	envoy_service_auth_v2 "github.com/envoyproxy/go-control-plane/envoy/service/auth/v2"
	envoy_service_auth_v3 "github.com/envoyproxy/go-control-plane/envoy/service/auth/v3"
	"google.golang.org/grpc"

	"github.com/envoyproxy/envoy/examples/ext_authz/auth/grpc-service/pkg/auth"
	auth_v2 "github.com/envoyproxy/envoy/examples/ext_authz/auth/grpc-service/pkg/auth/v2"
	auth_v3 "github.com/envoyproxy/envoy/examples/ext_authz/auth/grpc-service/pkg/auth/v3"
)

func main() {
	port := flag.Int("port", 9001, "gRPC port")
	data := flag.String("users", "../../users.json", "users file")

	flag.Parse()

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *port))
	if err != nil {
		log.Fatalf("failed to listen to %d: %v", *port, err)
	}

	users, err := auth.LoadUsers(*data)
	if err != nil {
		log.Fatalf("failed to load user data:%s %v", *data, err)
	}
	gs := grpc.NewServer()

	// Serve v3 and v2.
	envoy_service_auth_v3.RegisterAuthorizationServer(gs, auth_v3.New(users))
	envoy_service_auth_v2.RegisterAuthorizationServer(gs, auth_v2.New(users))

	log.Printf("starting gRPC server on: %d\n", *port)

	gs.Serve(lis)
}
