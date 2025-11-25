module example.com/routeconfig

go 1.24.6

require (
	github.com/cncf/xds/go v0.0.0-20251110193048-8bfbf64dc13e
	github.com/envoyproxy/envoy v1.36.2
	google.golang.org/protobuf v1.36.10
)

require (
	cel.dev/expr v0.24.0 // indirect
	github.com/envoyproxy/protoc-gen-validate v1.2.1 // indirect
	google.golang.org/genproto/googleapis/api v0.0.0-20251029180050-ab9386a59fda // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20251029180050-ab9386a59fda // indirect
)

replace github.com/envoyproxy/envoy => ../../../../../../../
