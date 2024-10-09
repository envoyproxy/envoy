module example.com/action

go 1.22

require github.com/envoyproxy/envoy v1.24.0

require google.golang.org/protobuf v1.33.0 // indirect

replace github.com/envoyproxy/envoy => ../../../../../../../
