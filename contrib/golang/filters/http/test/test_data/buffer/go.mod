module example.com/buffer

go 1.20

require (
	github.com/envoyproxy/envoy v1.24.0
	google.golang.org/protobuf v1.34.1
)

replace github.com/envoyproxy/envoy => ../../../../../../../
