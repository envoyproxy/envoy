package main

import (
	gosdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
)

func main() {} // c-shared must define the empty main function.

func init() {
	gosdk.OnProgramInit = func() bool { return true }
}
