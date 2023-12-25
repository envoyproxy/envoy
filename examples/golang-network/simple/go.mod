module github.com/envoyproxy/envoy/examples/golang-network/simple

// the version should >= 1.18
go 1.18

// NOTICE: these lines could be generated automatically by "go mod tidy"
require (
	github.com/cncf/xds/go v0.0.0-20230607035331-e9ce68804cb4
	github.com/envoyproxy/envoy v1.24.0
	google.golang.org/protobuf v1.32.0
)

require (
	github.com/envoyproxy/protoc-gen-validate v0.10.1 // indirect
	github.com/golang/protobuf v1.5.3 // indirect
	github.com/google/go-cmp v0.5.9 // indirect
	google.golang.org/genproto v0.0.0-20230410155749-daa745c078e1 // indirect
)

// TODO: remove when #26173 lands.
replace github.com/envoyproxy/envoy => ../../..
