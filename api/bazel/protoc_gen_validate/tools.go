//go:build tools
// +build tools

package main

import (
	_ "golang.org/x/net/context"
	_ "google.golang.org/protobuf/cmd/protoc-gen-go"
)
