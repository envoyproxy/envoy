module example.com/action

go 1.20

require (
	github.com/cncf/xds/go v0.0.0-20230112175826-46e39c7b9b43
	github.com/envoyproxy/envoy v1.24.0
)

require github.com/google/go-cmp v0.5.9 // indirect

require (
	github.com/envoyproxy/protoc-gen-validate v0.9.1 // indirect
	github.com/golang/protobuf v1.5.2 // indirect
	google.golang.org/genproto v0.0.0-20230110181048-76db0878b65f // indirect
	google.golang.org/protobuf v1.31.0
)

replace github.com/envoyproxy/envoy => ../../../../../../../
