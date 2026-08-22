module example.com/test-data

go 1.25.0

require (
	github.com/cncf/xds/go v0.0.0-20260202195803-dba9d589def2
	github.com/envoyproxy/envoy v1.36.2
	google.golang.org/protobuf v1.36.12
)

require (
	cel.dev/expr v0.25.1 // indirect
	github.com/envoyproxy/protoc-gen-validate v1.3.3 // indirect
	google.golang.org/genproto/googleapis/api v0.0.0-20260414002931-afd174a4e478 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20260414002931-afd174a4e478 // indirect
)

replace github.com/envoyproxy/envoy => ../../../../../../
