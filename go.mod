module github.com/envoyproxy/envoy

go 1.20

require (
	github.com/census-instrumentation/opencensus-proto v0.4.1
	github.com/cncf/xds/go v0.0.0-20231128003011-0fa0005c9caa
	github.com/envoyproxy/go-control-plane v0.12.0
	github.com/envoyproxy/protoc-gen-validate v1.0.2
	github.com/golang/protobuf v1.5.3
	github.com/prometheus/client_model v0.5.0
	go.opentelemetry.io/proto/otlp v1.0.0
	google.golang.org/genproto/googleapis/api v0.0.0-20240102182953-50ed04b92917
	google.golang.org/genproto/googleapis/rpc v0.0.0-20240102182953-50ed04b92917
	google.golang.org/grpc v1.60.1
	google.golang.org/protobuf v1.32.0
)

require (
	golang.org/x/net v0.17.0 // indirect
	golang.org/x/sys v0.13.0 // indirect
	golang.org/x/text v0.13.0 // indirect
	google.golang.org/genproto v0.0.0-20231212172506-995d672761c0 // indirect
)
