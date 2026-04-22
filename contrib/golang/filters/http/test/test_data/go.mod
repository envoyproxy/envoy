module example.com/test-data

go 1.24.6

require (
	github.com/cncf/xds/go v0.0.0-20251110193048-8bfbf64dc13e
	github.com/envoyproxy/envoy v1.36.2
	google.golang.org/protobuf v1.36.11
)

require (
	cel.dev/expr v0.25.1 // indirect
	github.com/envoyproxy/protoc-gen-validate v1.2.1 // indirect
	google.golang.org/genproto/googleapis/api v0.0.0-20250728155136-f173205681a0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20250728155136-f173205681a0 // indirect
)

replace github.com/envoyproxy/envoy => ../../../../../../
