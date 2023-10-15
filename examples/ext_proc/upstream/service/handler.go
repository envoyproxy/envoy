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
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"regexp"
	"strconv"

	"github.com/julienschmidt/httprouter"
)

const (
	defaultSize = 100
	pattern     = "0123456789"
)

const helpMessage = `GET  /                 : Print default message
GET  /help             : Print this message
POST /echo             : Echo back whatever you posted
GET  /json             : Return JSON content.
                         Use optional "size" parameter for extra data.
GET  /json-trailers    : Return JSON content with the trailer 
                         "x-test-target: Yes"
GET  /data?size=xxx    : Return arbitrary characters xxx bytes long
POST /database         : Store arbitrary JSON, looking for a field
                         named "key" as the key
GET  /database/{key}   : Return the record stored by POST to /database
DELETE /database/{key} : Remove the record stored by POST to /database
PUT /database/{key}    : Update the record stored by POST to /database,
                         or 404 if not found
`

var jsonContent = regexp.MustCompile("^application/json(;.*)?$")

type sampleJSONMessage struct {
	Testing       int    `json:"testing"`
	IsTesting     bool   `json:"isTesting"`
	HowTestyAreWe string `json:"howTestyAreWe"`
	ExtraData     []byte `json:"extraData,omitempty"`
}

type databaseRecord struct {
	Key string `json:"key"`
	// The rest of the structure is opaque to us
}

func createHandler() http.Handler {
	data := make(map[string][]byte)
	router := httprouter.New()
	router.GET("/", handleIndex)
	router.GET("/help", handleHelp)
	router.GET("/hello", handleHello)
	router.POST("/echo", handleEcho)
	router.GET("/json", handleJSON)
	router.GET("/json-trailers", handleJSONWithTrailers)
	router.GET("/data", handleData)
	router.POST("/database", func(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
		handleDatabasePost(resp, req, data)
	})
	router.GET("/database/:key", func(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
		handleDatabaseGet(resp, req, params, data)
	})
	router.PUT("/database/:key", func(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
		handleDatabasePut(resp, req, params, data)
	})
	router.DELETE("/database/:key", func(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
		handleDatabaseDelete(resp, req, params, data)
	})
	return router
}

func handleIndex(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
	resp.Header().Add("content-type", "text/plain")
	resp.WriteHeader(http.StatusOK)
	resp.Write([]byte("Use /help to find out what is possible\n"))
}

func handleHelp(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
	resp.Header().Add("content-type", "text/plain")
	resp.WriteHeader(http.StatusOK)
	resp.Write([]byte(helpMessage))
}

func handleHello(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
	resp.Header().Add("content-type", "text/plain")
	resp.WriteHeader(http.StatusOK)
	resp.Write([]byte("Hello, World!"))
}

func handleEcho(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
	contentType := req.Header.Get("content-type")
	resp.Header().Add("content-type", contentType)
	io.Copy(resp, req.Body)
}

func handleJSON(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
	optionalSize := parseSize(req, 0)
	msg := sampleJSONMessage{
		Testing:       123,
		IsTesting:     true,
		HowTestyAreWe: "Very!",
	}
	if optionalSize > 0 {
		msg.ExtraData = makeData(optionalSize)
	}
	resp.Header().Add("content-type", "application/json")
	resp.WriteHeader(http.StatusOK)
	enc := json.NewEncoder(resp)
	enc.Encode(&msg)
}

func handleJSONWithTrailers(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
	msg := sampleJSONMessage{
		Testing:       123,
		IsTesting:     true,
		HowTestyAreWe: "Very!",
	}
	resp.Header().Add("content-type", "application/json")
	resp.Header().Add("Trailer", "x-test-target")
	resp.WriteHeader(http.StatusOK)
	enc := json.NewEncoder(resp)
	enc.Encode(&msg)
	resp.Header().Add("x-test-target", "Yes")
}

func handleData(resp http.ResponseWriter, req *http.Request, params httprouter.Params) {
	size := parseSize(req, defaultSize)
	resp.Header().Add("content-type", "application/octet-stream")
	resp.Header().Add("content-length", strconv.Itoa(size))
	resp.Write(makeData(size))
}

func readJSONBody(resp http.ResponseWriter, req *http.Request) ([]byte, *databaseRecord, error) {
	if !jsonContent.MatchString(req.Header.Get("Content-Type")) {
		sendError(415, "Only JSON content is supported", resp)
		return nil, nil, errors.New("Invalid content type")
	}
	body, _ := ioutil.ReadAll(req.Body)
	var record databaseRecord
	err := json.Unmarshal(body, &record)
	if err != nil {
		sendError(400, fmt.Sprintf("Invalid JSON: %s", err), resp)
		return nil, nil, errors.New("Invalid JSON")
	}
	if record.Key == "" {
		sendError(400, "Missing \"key\" property", resp)
		return nil, nil, errors.New("Invalid content")
	}
	return body, &record, nil
}

func handleDatabasePost(resp http.ResponseWriter, req *http.Request, data map[string][]byte) {
	body, record, err := readJSONBody(resp, req)
	if err == nil {
		data[record.Key] = body
		resp.WriteHeader(201)
	}
}

func handleDatabaseGet(resp http.ResponseWriter, req *http.Request, params httprouter.Params, data map[string][]byte) {
	record := data[params.ByName("key")]
	if record == nil {
		sendError(404, "Not found", resp)
	} else {
		resp.Header().Set("Content-Type", "application/json")
		resp.Header().Set("Content-Length", strconv.Itoa(len(record)))
		resp.Write(record)
	}
}

func handleDatabasePut(resp http.ResponseWriter, req *http.Request, params httprouter.Params, data map[string][]byte) {
	key := params.ByName("key")
	recordFound := data[key]
	if recordFound == nil {
		sendError(404, "Not found", resp)
		return
	}
	body, newRecord, err := readJSONBody(resp, req)
	if err == nil {
		if newRecord.Key != key {
			sendError(400, "Key does not match", resp)
		} else {
			data[newRecord.Key] = body
		}
	}
}

func handleDatabaseDelete(resp http.ResponseWriter, req *http.Request, params httprouter.Params, data map[string][]byte) {
	key := params.ByName("key")
	record := data[key]
	if record == nil {
		sendError(404, "Not found", resp)
	} else {
		delete(data, key)
	}
}

func parseSize(req *http.Request, defaultSize int) int {
	sizeStr := req.URL.Query().Get("size")
	if sizeStr == "" {
		return defaultSize
	}
	size, _ := strconv.Atoi(sizeStr)
	return size
}

func makeData(size int) []byte {
	buf := bytes.Buffer{}
	for pos := 0; pos < size; pos += 10 {
		remaining := size - pos
		if remaining < 10 {
			buf.Write([]byte(pattern[:remaining]))
		} else {
			buf.Write([]byte(pattern))
		}
	}
	return buf.Bytes()
}

func sendError(statusCode int, message string, resp http.ResponseWriter) {
	if message != "" {
		resp.Header().Add("Content-Type", "text/plain")
	}
	resp.WriteHeader(statusCode)
	if message != "" {
		resp.Write([]byte(message))
	}
}