module example.com/property

go 1.22

require (
	github.com/envoyproxy/envoy v1.24.0
	google.golang.org/protobuf v1.35.2
)

replace github.com/envoyproxy/envoy => ../../../../../../../
