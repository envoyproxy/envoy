module github.com/envoyproxy/envoy/examples/golang-network/simple

go 1.18

replace github.com/envoyproxy/envoy/contrib/golang/filters/go => ../../../contrib/golang/filters/go

require (
	github.com/cncf/xds/go v0.0.0-20230310173818-32f1caf87195
	github.com/envoyproxy/envoy/contrib/golang/filters/go v0.0.0-00010101000000-000000000000
	google.golang.org/protobuf v1.30.0
)

require (
	github.com/envoyproxy/envoy/contrib/golang v0.0.0-20230401052351-e665d4292d40 // indirect
	github.com/envoyproxy/protoc-gen-validate v0.1.0 // indirect
	github.com/golang/protobuf v1.5.0 // indirect
	golang.org/x/net v0.0.0-20190311183353-d8887717615a // indirect
	golang.org/x/sys v0.0.0-20190215142949-d0b11bdaac8a // indirect
	golang.org/x/text v0.3.0 // indirect
	google.golang.org/genproto v0.0.0-20190819201941-24fa4b261c55 // indirect
	google.golang.org/grpc v1.25.1 // indirect
)
