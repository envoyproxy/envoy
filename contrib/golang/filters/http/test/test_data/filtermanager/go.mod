module example.com/filtermanager

go 1.20

require (
	github.com/cncf/xds/go v0.0.0-20231011190438-776c4db845ed
	github.com/envoyproxy/envoy v1.24.0
)

require (
	github.com/envoyproxy/protoc-gen-validate v0.1.0 // indirect
	github.com/golang/protobuf v1.5.0 // indirect
	golang.org/x/net v0.17.0 // indirect
	golang.org/x/sys v0.13.0 // indirect
	golang.org/x/text v0.13.0 // indirect
	google.golang.org/genproto v0.0.0-20190819201941-24fa4b261c55 // indirect
	google.golang.org/grpc v1.25.1 // indirect
	google.golang.org/protobuf v1.31.0
)

replace github.com/envoyproxy/envoy => ../../../../../../../
