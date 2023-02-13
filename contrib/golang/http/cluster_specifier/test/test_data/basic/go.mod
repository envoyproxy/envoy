module test.com/basic

go 1.19

require github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go v0.0.0-20230130041725-04a1ff966ea5

require google.golang.org/protobuf v1.28.1 // indirect

replace github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go => ../../../source/go/