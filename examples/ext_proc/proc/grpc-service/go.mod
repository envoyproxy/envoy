module github.com/envoyproxy/envoy/examples/ext_proc/proc/grpc-service

go 1.16

require (
	github.com/envoyproxy/envoy/examples/ext_authz/auth/grpc-service v0.0.0-20231024234108-b9e4260466f7
	github.com/envoyproxy/go-control-plane v0.11.2-0.20231010142055-b65248991f33
	github.com/golang/protobuf v1.5.3
	go.uber.org/zap v1.20.0
	golang.org/x/net v0.17.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20230822172742-b8732ec3820d
	google.golang.org/grpc v1.59.0
)
