module example.com/test-data

go 1.22

require github.com/envoyproxy/envoy v1.24.0

require (
	github.com/cncf/xds/go v0.0.0-20241223141626-cff3c89139a3
	google.golang.org/protobuf v1.36.2
)

require (
	cel.dev/expr v0.15.0 // indirect
	github.com/envoyproxy/protoc-gen-validate v1.0.4 // indirect
	google.golang.org/genproto/googleapis/api v0.0.0-20230822172742-b8732ec3820d // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20240318140521-94a12d6c2237 // indirect
)

replace github.com/envoyproxy/envoy => ../../../../../../
