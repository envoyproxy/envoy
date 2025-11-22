module example.com/test-data

go 1.24.6

require (
	github.com/cncf/xds/go v0.0.0-20251210132809-ee656c7534f5
	github.com/envoyproxy/envoy v1.36.2
	google.golang.org/protobuf v1.36.10
)

require (
	cel.dev/expr v0.25.1 // indirect
	github.com/envoyproxy/protoc-gen-validate v1.2.1 // indirect
	google.golang.org/genproto/googleapis/api v0.0.0-20240903143218-8af14fe29dc1 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20240903143218-8af14fe29dc1 // indirect
)

replace github.com/envoyproxy/envoy => ../../../../../../
