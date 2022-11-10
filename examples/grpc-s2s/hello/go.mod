module github.com/envoy/examples/grpc-s2s/hello

replace github.com/envoy/examples/grpc-s2s/service => ../protos

go 1.19

require (
	github.com/envoy/examples/grpc-s2s/service v0.0.0-00010101000000-000000000000
	golang.org/x/net v0.2.0
	google.golang.org/grpc v1.50.1
)

require (
	github.com/golang/protobuf v1.5.2 // indirect
	golang.org/x/sys v0.2.0 // indirect
	golang.org/x/text v0.4.0 // indirect
	google.golang.org/genproto v0.0.0-20200526211855-cb27e3aa2013 // indirect
	google.golang.org/protobuf v1.28.1 // indirect
)
