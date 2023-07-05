module github.com/envoyproxy/envoy/examples/golang/simple

// the version should >= 1.18
go 1.18

// NOTICE: these lines could be generated automatically by "go mod tidy"
require (
	github.com/cncf/xds/go v0.0.0-20230310173818-32f1caf87195
	github.com/envoyproxy/envoy v1.24.0
	google.golang.org/protobuf v1.31.0
)

require (
	github.com/envoyproxy/protoc-gen-validate v0.1.0 // indirect
	github.com/golang/protobuf v1.5.0 // indirect
	golang.org/x/net v0.7.0 // indirect
	golang.org/x/sys v0.5.0 // indirect
	golang.org/x/text v0.7.0 // indirect
	google.golang.org/genproto v0.0.0-20190819201941-24fa4b261c55 // indirect
	google.golang.org/grpc v1.25.1 // indirect
)

// NOTICE: it's just for testing, please remove it.
// And check the "API compatibility" section in doc:
// https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/golang_filter#developing-a-go-plugin
replace github.com/envoyproxy/envoy => ../../..
