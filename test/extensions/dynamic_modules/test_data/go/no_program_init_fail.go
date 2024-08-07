package main

import (
	envoy "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
)

func main() {} // c-shared must define the empty main function.

func init() {
	envoy.OnProgramInit = func() int { return 12345 }
}
