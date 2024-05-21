module example.com/routeconfig

go 1.18

require (
	github.com/cncf/xds/go v0.0.0-20230607035331-e9ce68804cb4
	github.com/envoyproxy/envoy v1.28.0
)

require github.com/google/go-cmp v0.5.9 // indirect

require (
	github.com/envoyproxy/protoc-gen-validate v0.10.1 // indirect
	github.com/golang/protobuf v1.5.3 // indirect
	google.golang.org/protobuf v1.33.0
)

replace github.com/envoyproxy/envoy => ../../../../../../../
