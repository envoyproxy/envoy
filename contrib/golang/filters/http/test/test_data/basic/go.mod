module example.com/basic

go 1.20

require (
	github.com/envoyproxy/envoy v1.24.0
	golang.org/x/exp v0.0.0-20240506185415-9bf2ced13842
)

require google.golang.org/protobuf v1.33.0 // indirect

replace github.com/envoyproxy/envoy => ../../../../../../../
