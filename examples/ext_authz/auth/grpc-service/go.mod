module github.com/envoyproxy/envoy/examples/ext_authz/auth/grpc-service

go 1.21

toolchain go1.22.5

require (
	github.com/envoyproxy/go-control-plane v0.12.0
	github.com/golang/protobuf v1.5.4
	google.golang.org/genproto/googleapis/rpc v0.0.0-20240528184218-531527333157
	google.golang.org/grpc v1.65.0
)

require (
	github.com/cncf/xds/go v0.0.0-20240423153145-555b57ec207b // indirect
	github.com/envoyproxy/protoc-gen-validate v1.0.4 // indirect
	golang.org/x/net v0.26.0 // indirect
	golang.org/x/sys v0.21.0 // indirect
	golang.org/x/text v0.16.0 // indirect
	google.golang.org/protobuf v1.34.1 // indirect
)
