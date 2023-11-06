// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"flag"
	"fmt"
	"net/http"
	"os"

	"go.uber.org/zap"
	"golang.org/x/net/http2"
	"golang.org/x/net/http2/h2c"
)

var logger *zap.SugaredLogger = nil

func main() {
	var doHelp bool
	var debug bool
	var port int

	flag.BoolVar(&doHelp, "h", false, "Print help message")
	flag.BoolVar(&debug, "d", false, "Enable debug logging")
	flag.IntVar(&port, "p", -1, "TCP listen port")
	flag.Parse()

	if !flag.Parsed() || doHelp || port < 0 {
		flag.PrintDefaults()
		os.Exit(2)
	}

	var err error
	var zapLogger *zap.Logger
	if debug {
		zapLogger, err = zap.NewDevelopment()
	} else {
		zapLogger, err = zap.NewProduction()
	}
	if err != nil {
		panic(fmt.Sprintf("Can't initialize logger: %s", err))
	}
	logger = zapLogger.Sugar()

	// This extra stuff lets us support HTTP/2 without
	// TLS using the "h2c" extension.
	handler := createHandler()
	h2Server := http2.Server{}
	server := http.Server{
		Addr:    fmt.Sprintf("0.0.0.0:%d", port),
		Handler: h2c.NewHandler(handler, &h2Server),
	}
	server.ListenAndServe()
}
