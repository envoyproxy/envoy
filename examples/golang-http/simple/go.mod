module github.com/envoyproxy/envoy/examples/golang-http/simple

// the version should >= 1.18
go 1.20

// NOTICE: these lines could be generated automatically by "go mod tidy"
require (
	github.com/cncf/xds/go v0.0.0-20230310173818-32f1caf87195
	github.com/envoyproxy/envoy v1.24.0
	google.golang.org/protobuf v1.31.0
)

require (
	github.com/envoyproxy/protoc-gen-validate v0.9.1 // indirect
	github.com/golang/protobuf v1.5.2 // indirect
	github.com/google/go-cmp v0.5.9 // indirect
	google.golang.org/genproto v0.0.0-20230110181048-76db0878b65f // indirect
)

// TODO: remove when #26173 lands.
// And check the "API compatibility" section in doc:
// https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/golang_filter#developing-a-go-plugin
replace github.com/envoyproxy/envoy => ../../..
