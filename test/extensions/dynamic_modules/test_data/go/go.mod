module main

go 1.22.5

require github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go v0.0.0

replace (
	github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go => ../../../../../source/extensions/dynamic_modules/sdk/go/
)
