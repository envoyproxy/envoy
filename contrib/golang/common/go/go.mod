module github.com/envoyproxy/envoy/contrib/golang/common/go

go 1.18

require (
	github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go v0.0.0-00010101000000-000000000000
	github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go v0.0.0-00010101000000-000000000000
)

require google.golang.org/protobuf v1.28.1 // indirect

replace github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go => ../../filters/http/source/go

replace github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go => ../../http/cluster_specifier/source/go
