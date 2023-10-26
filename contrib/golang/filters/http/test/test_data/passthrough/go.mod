module example.com/passthrough

go 1.20

require github.com/envoyproxy/envoy v1.24.0

require google.golang.org/protobuf v1.30.0 // indirect

replace github.com/envoyproxy/envoy => ../../../../../../../
