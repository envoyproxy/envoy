module github.com/envoyproxy/envoy/examples/golang-network/simple

// the version should >= 1.18
go 1.18

// NOTICE: these lines could be generated automatically by "go mod tidy"
require (
	github.com/cncf/xds/go v0.0.0-20231128003011-0fa0005c9caa
	github.com/envoyproxy/envoy v1.24.0
	google.golang.org/protobuf v1.32.0
)

require (
	github.com/envoyproxy/protoc-gen-validate v1.0.2 // indirect
	github.com/golang/protobuf v1.5.3 // indirect
	google.golang.org/genproto/googleapis/api v0.0.0-20240102182953-50ed04b92917 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20240102182953-50ed04b92917 // indirect
)

// TODO: remove when #26173 lands.
replace github.com/envoyproxy/envoy => ../../..
