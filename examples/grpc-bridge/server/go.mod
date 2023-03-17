module github.com/envoyproxy/envoy

go 1.13

require (
	github.com/envoyproxy/envoy/examples/grpc-bridge/server/kv v0.0.0-00010101000000-000000000000
	golang.org/x/net v0.0.0-20220708220712-1185a9018129
	golang.org/x/sys v0.0.0-20220708085239-5a0f0661e09d // indirect
	google.golang.org/genproto v0.0.0-20220708155623-50e5f4832e73 // indirect
	google.golang.org/grpc v1.47.0
)

replace github.com/envoyproxy/envoy/examples/grpc-bridge/server/kv => ./kv
