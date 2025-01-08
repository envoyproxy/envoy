module example.com/routeconfig

go 1.22

require (
	github.com/envoyproxy/envoy v1.28.0
)

require (
	google.golang.org/protobuf v1.35.2
)

replace github.com/envoyproxy/envoy => ../../../../../../../..
