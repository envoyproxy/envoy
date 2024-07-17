module example.com/echo

go 1.20

require (
	github.com/cncf/xds/go v0.0.0-20231128003011-0fa0005c9caa
	github.com/envoyproxy/envoy v1.24.0
)

require (
	google.golang.org/genproto/googleapis/api v0.0.0-20240102182953-50ed04b92917 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20240102182953-50ed04b92917 // indirect
)

require (
	github.com/envoyproxy/protoc-gen-validate v1.0.2 // indirect
	github.com/golang/protobuf v1.5.3 // indirect
	google.golang.org/protobuf v1.34.2
)

replace github.com/envoyproxy/envoy => ../../../../../../../
