module example.com/property

go 1.20

require (
	github.com/envoyproxy/envoy v1.24.0
	google.golang.org/protobuf v1.34.0
)

replace github.com/envoyproxy/envoy => ../../../../../../../
