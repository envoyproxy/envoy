module example.com/action

go 1.20

require github.com/envoyproxy/envoy v1.24.0

require github.com/google/go-cmp v0.5.9 // indirect

require google.golang.org/protobuf v1.32.0 // indirect

replace github.com/envoyproxy/envoy => ../../../../../../../
