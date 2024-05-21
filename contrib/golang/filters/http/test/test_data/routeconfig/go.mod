module example.com/routeconfig

go 1.20

require (
	github.com/cncf/xds/go v0.0.0-20230112175826-46e39c7b9b43
	github.com/envoyproxy/envoy v1.24.0
)

require github.com/google/go-cmp v0.5.9 // indirect

require (
	github.com/envoyproxy/protoc-gen-validate v1.0.2 // indirect
	github.com/golang/protobuf v1.5.3 // indirect
	google.golang.org/protobuf v1.33.0
)

replace github.com/envoyproxy/envoy => ../../../../../../../
