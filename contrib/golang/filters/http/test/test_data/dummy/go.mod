module example.com/dummy

go 1.18

require github.com/envoyproxy/envoy/contrib/golang v1.24.0

require google.golang.org/protobuf v1.28.1 // indirect

replace github.com/envoyproxy/envoy/contrib/golang => ../../../../../
