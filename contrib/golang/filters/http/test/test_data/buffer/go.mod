module example.com/buffer

go 1.22

require (
	github.com/envoyproxy/envoy v1.24.0
	google.golang.org/protobuf v1.36.0
)

replace github.com/envoyproxy/envoy => ../../../../../../../
