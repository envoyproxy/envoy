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
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"regexp"

	core "github.com/envoyproxy/go-control-plane/envoy/config/core/v3"
	extproc_cfg "github.com/envoyproxy/go-control-plane/envoy/extensions/filters/http/ext_proc/v3"
	extproc "github.com/envoyproxy/go-control-plane/envoy/service/ext_proc/v3"
	envoy_type "github.com/envoyproxy/go-control-plane/envoy/type/v3"
)

var contentTypeJSON = regexp.MustCompile("^(application|text)/json(;.*)?$")

type processorService struct{}

func (s *processorService) Process(stream extproc.ExternalProcessor_ProcessServer) error {
	msg, err := stream.Recv()
	if err == io.EOF {
		logger.Debug("Stream closed by proxy")
		return nil
	}
	if err != nil {
		logger.Errorf("Error receiving from stream: %s", err)
		return err
	}

	logger.Debug("First message: %b", msg)
	headers := msg.GetRequestHeaders()
	if headers == nil {
		logger.Warn("Expecting request headers message first")
		// Close stream since there's nothing else we can do
		return nil
	}

	path := getHeaderValue(headers.Headers, ":path")
	logger.Debugf("Received request headers for %s", path)

	switch path {
	case "/echo", "/help", "/hello", "/json":
		// Pass through the basic ones so that the test target works as designed.
		// Just close the stream, which indicates "no more processing"
		return processAndJustLog(stream)
	case "/echohashstream":
		return processEchoHashStreaming(stream)
	case "/echohashbuffered":
		return processEchoHashBuffered(stream, extproc_cfg.ProcessingMode_BUFFERED)
	case "/echohashbufferedpartial":
		return processEchoHashBuffered(stream, extproc_cfg.ProcessingMode_BUFFERED_PARTIAL)
	case "/echoencode":
		return processEncodeDecode(stream)
	case "/addHeader", "/testAddHeader":
		return processAddHeader(stream)
	case "/checkJson":
		return processCheckJSON(stream, headers)
	case "/getToPost":
		return processGetToPost(stream, headers)
	case "/notfound":
		return processNotFound(stream)
	}

	// Do nothing for any unknown paths
	return nil
}

// Just log what ext_proc sends us
func processAndJustLog(stream extproc.ExternalProcessor_ProcessServer) error {
	response := &extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_RequestHeaders{},
	}
	stream.Send(response)

	for {
		msg, err := stream.Recv()
		if err == io.EOF {
			logger.Debug("EOF")
			return nil
		} else if err != nil {
			logger.Debugf("Error on stream: %v", err)
			return err
		}

		response = &extproc.ProcessingResponse{}

		if msg.GetRequestBody() != nil {
			logger.Debug("Got request body")
			response.Response = &extproc.ProcessingResponse_RequestBody{}
		} else if msg.GetRequestTrailers() != nil {
			logger.Debug("Got request trailers")
			response.Response = &extproc.ProcessingResponse_RequestTrailers{}
		} else if msg.GetResponseHeaders() != nil {
			logger.Debug("Got response headers")
			response.Response = &extproc.ProcessingResponse_ResponseHeaders{}
		} else if msg.GetResponseBody() != nil {
			logger.Debug("Got response body")
			response.Response = &extproc.ProcessingResponse_ResponseBody{}
		} else if msg.GetResponseTrailers() != nil {
			logger.Debug("Got response trailers")
			response.Response = &extproc.ProcessingResponse_ResponseTrailers{}
		} else {
			logger.Debug("Got a totally unrecognized message!")
			return nil
		}

		stream.Send(response)
	}
}

// Show how to return an error by returning a 404 in response to the "/notfound"
// path.
func processNotFound(stream extproc.ExternalProcessor_ProcessServer) error {
	// Construct an "immediate" response that will go back to the caller. It will
	// have:
	// * A status code of 404
	// * a content-type header of "text/plain"
	// * a body that says "Not found"
	// * an additional message that may be logged by envoy
	logger.Debug("Sending back immediate 404 response")
	response := &extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_ImmediateResponse{
			ImmediateResponse: &extproc.ImmediateResponse{
				Status: &envoy_type.HttpStatus{
					Code: envoy_type.StatusCode_NotFound,
				},
				Headers: &extproc.HeaderMutation{
					SetHeaders: []*core.HeaderValueOption{
						{
							Header: &core.HeaderValue{
								Key:      "content-type",
								RawValue: []byte("text/plain"),
							},
						},
					},
				},
				Body:    "Not found",
				Details: "Requested path was not found",
			},
		},
	}
	// Send the message and return, which closes the stream
	return stream.Send(response)
}

// Show how to add a header to the response.
func processAddHeader(stream extproc.ExternalProcessor_ProcessServer) error {
	// Change the path to "/hello" because that's one of the paths that the target
	// server understands. (Sadly, go syntax and the way that it handles "oneof"
	// messages means a lot of boilerplate here!)
	logger.Debug("Redirecting target URL to /hello")
	err := stream.Send(&extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_RequestHeaders{
			RequestHeaders: &extproc.HeadersResponse{
				Response: &extproc.CommonResponse{
					HeaderMutation: &extproc.HeaderMutation{
						SetHeaders: []*core.HeaderValueOption{
							{
								Header: &core.HeaderValue{
									Key:      ":path",
									RawValue: []byte("/hello"),
								},
							},
						},
					},
				},
			},
		},
	})
	if err != nil {
		return err
	}

	msg, err := stream.Recv()
	if err != nil {
		return err
	}

	// We have not changed the processing mode, so the next message will be a
	// response headers message, and we should close the stream if it is not.
	responseHeaders := msg.GetResponseHeaders()
	if responseHeaders == nil {
		logger.Error("Expecting response headers as the next message")
		return nil
	}
	logger.Debug("Got response headers and sending response")

	// Send back a response that adds a header
	response := &extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_ResponseHeaders{
			ResponseHeaders: &extproc.HeadersResponse{
				Response: &extproc.CommonResponse{
					HeaderMutation: &extproc.HeaderMutation{
						SetHeaders: []*core.HeaderValueOption{
							{
								Header: &core.HeaderValue{
									Key:      "x-external-processor-status",
									RawValue: []byte("We were here"),
								},
							},
						},
					},
				},
			},
		},
	}
	return stream.Send(response)
}

// This is a more sophisticated example that does a few things:
//  1. It sets a request header to rewrite the path
//  2. It checks the content-type header to see if it is JSON (or skips
//     this check if there is no content
//  3. If it is JSON, it changes the processing mode to get the request body
//  4. If the content is JSON, validate the request body as JSON
//  5. Return an error if the validation fails
//  6. Or, add a response header to reflect the request validation status
//
// Among other things, this example shows the ablility of an external processor
// to decide how much of the request and response it needs to process based
// on input.
func processCheckJSON(stream extproc.ExternalProcessor_ProcessServer,
	requestHeaders *extproc.HttpHeaders) error {
	// Set the path to "/echo" to get the right functionality on the target.
	logger.Debug("Setting target URL to /echo")
	requestHeadersResponse := &extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_RequestHeaders{
			RequestHeaders: &extproc.HeadersResponse{
				Response: &extproc.CommonResponse{
					HeaderMutation: &extproc.HeaderMutation{
						SetHeaders: []*core.HeaderValueOption{
							{
								Header: &core.HeaderValue{
									Key:      ":path",
									RawValue: []byte("/echo"),
								},
							},
						},
					},
				},
			},
		},
	}

	// If the content-type header looks like JSON, then ask for the request body
	contentType := getHeaderValue(requestHeaders.Headers, "content-type")
	logger.Debugf("Checking content-type %s to see if it is JSON", contentType)
	contentIsJSON := !requestHeaders.EndOfStream && contentTypeJSON.MatchString(contentType)
	if contentIsJSON {
		logger.Debug("Requesting buffered request body")
		requestHeadersResponse.ModeOverride = &extproc_cfg.ProcessingMode{
			RequestBodyMode: extproc_cfg.ProcessingMode_BUFFERED,
		}
	}

	err := stream.Send(requestHeadersResponse)
	if err != nil {
		return err
	}

	var msg *extproc.ProcessingRequest
	var jsonStatus string

	if contentIsJSON {
		msg, err = stream.Recv()
		if err != nil {
			return err
		}

		requestBody := msg.GetRequestBody()
		if requestBody == nil {
			logger.Error("Expected request body to be sent next")
			return nil
		}

		requestBodyResponse := &extproc.ProcessingResponse{}

		if json.Valid(requestBody.Body) {
			// Nothing to do, so return an empty response
			requestBodyResponse.Response = &extproc.ProcessingResponse_RequestBody{}

		} else {
			requestBodyResponse.Response = &extproc.ProcessingResponse_ImmediateResponse{
				ImmediateResponse: &extproc.ImmediateResponse{
					Status: &envoy_type.HttpStatus{
						Code: envoy_type.StatusCode_BadRequest,
					},
					Headers: &extproc.HeaderMutation{
						SetHeaders: []*core.HeaderValueOption{
							{
								Header: &core.HeaderValue{
									Key:      "content-type",
									RawValue: []byte("text/plain"),
								},
							},
						},
					},
					Body:    "Invalid JSON",
					Details: "Request body was not valid JSON",
				},
			}
			// Send the error response and end processing now
			return stream.Send(requestBodyResponse)
		}

		// Send the body response and wait for the next message
		err = stream.Send(requestBodyResponse)
		if err != nil {
			return err
		}
		jsonStatus = "Body is valid JSON"

	} else {
		jsonStatus = "Body is not JSON"
	}

	msg, err = stream.Recv()
	if err != nil {
		return err
	}

	responseHeaders := msg.GetResponseHeaders()
	if responseHeaders == nil {
		logger.Error("Expecting response headers as the next message")
		return nil
	}

	responseHeadersResponse := &extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_ResponseHeaders{
			ResponseHeaders: &extproc.HeadersResponse{
				Response: &extproc.CommonResponse{
					HeaderMutation: &extproc.HeaderMutation{
						SetHeaders: []*core.HeaderValueOption{
							{
								Header: &core.HeaderValue{
									Key:      "x-json-status",
									RawValue: []byte(jsonStatus),
								},
							},
						},
					},
				},
			},
		},
	}
	return stream.Send(responseHeadersResponse)
}

const newJSONMessage = `
{
  "message": "Hello!",
  "recipients": ["World"]
}
`

// This replaces the incoming HTTP request with a POST
func processGetToPost(stream extproc.ExternalProcessor_ProcessServer,
	requestHeaders *extproc.HttpHeaders) error {
	requestHeadersResponse := &extproc.HeadersResponse{
		Response: &extproc.CommonResponse{
			Status:         extproc.CommonResponse_CONTINUE_AND_REPLACE,
			HeaderMutation: &extproc.HeaderMutation{},
			BodyMutation: &extproc.BodyMutation{
				Mutation: &extproc.BodyMutation_Body{
					Body: []byte(newJSONMessage),
				},
			},
		},
	}
	setHeader(requestHeadersResponse.Response.HeaderMutation, ":method", "POST")
	setHeader(requestHeadersResponse.Response.HeaderMutation, ":path", "/echo")
	setHeader(requestHeadersResponse.Response.HeaderMutation, "content-type", "application/json")
	return stream.Send(&extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_RequestHeaders{
			RequestHeaders: requestHeadersResponse,
		},
	})
}

// This example processes the request and response bodies using streaming mode,
// calculates a SHA-256 hash for each, and prints out both.
// (With the "/echo" target on the HTTP target in this
// project, they should always match.)
//
// In STREAMED mode, we can't manipulate the headers after a chunk of the body
// has been delivered, and in this particular example, it's possible that the
// request and response body chunks will be interleaved. So, we can't add the
// hash of the response body to the response headers, and we can't add the hash of
// the request body to the response headers either, since the response headers
// might be transmitted before the entire request body. (Try it with a big
// message and see!)
func processEchoHashStreaming(stream extproc.ExternalProcessor_ProcessServer) error {
	logger.Debug("processEchoHashStreaming")
	// We already have the request headers, so first send back the request body
	// message.
	logger.Debug("Setting target URL to /echo")
	err := stream.Send(&extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_RequestHeaders{
			RequestHeaders: &extproc.HeadersResponse{
				Response: &extproc.CommonResponse{
					HeaderMutation: &extproc.HeaderMutation{
						SetHeaders: []*core.HeaderValueOption{
							{
								Header: &core.HeaderValue{
									Key:      ":path",
									RawValue: []byte("/echo"),
								},
							},
						},
					},
				},
			},
		},
		ModeOverride: &extproc_cfg.ProcessingMode{
			RequestBodyMode:  extproc_cfg.ProcessingMode_STREAMED,
			ResponseBodyMode: extproc_cfg.ProcessingMode_STREAMED,
		},
	})
	if err != nil {
		return err
	}

	// When streaming, the request will always come before the request body
	// or the response, but other than that everything will be interleaved.
	// For that reason, we'll pull messages from the stream and react to
	// whatever we get.
	requestBodyDone := false
	responseBodyDone := false
	requestHash := sha256.New()
	responseHash := sha256.New()

	for !requestBodyDone || !responseBodyDone {
		msg, err := stream.Recv()
		if err == io.EOF {
			return nil
		} else if err != nil {
			logger.Warnf("Error receiving from stream: %v", err)
			return err
		}

		if msg.GetRequestBody() != nil {
			bod := msg.GetRequestBody()
			logger.Debugf("Request body: %d bytes. End = %v",
				len(bod.Body), bod.EndOfStream)
			requestHash.Write(bod.Body)
			requestBodyDone = bod.EndOfStream
			stream.Send(&extproc.ProcessingResponse{
				Response: &extproc.ProcessingResponse_RequestBody{},
			})

		} else if msg.GetResponseBody() != nil {
			bod := msg.GetResponseBody()
			logger.Debugf("Response body: %d bytes. End = %v",
				len(bod.Body), bod.EndOfStream)
			responseHash.Write(bod.Body)
			responseBodyDone = bod.EndOfStream
			stream.Send(&extproc.ProcessingResponse{
				Response: &extproc.ProcessingResponse_ResponseBody{},
			})

		} else if msg.GetResponseHeaders() != nil {
			logger.Debug("Got response headers")
			stream.Send(&extproc.ProcessingResponse{
				Response: &extproc.ProcessingResponse_ResponseHeaders{},
			})

		} else {
			logger.Debug("Got an unknown message")
		}
	}

	logger.Infof("Request body hash:  %x", requestHash.Sum(nil))
	logger.Infof("Response body hash: %x", responseHash.Sum(nil))
	return nil
}

// This also hashes the request and response bodies, but it uses a buffered mode.
// This holds the header until the entire request body is processed. This way, we
// can add a response header that shows both the request and response hashes.
// In BUFFERED mode, the request or response will fail if either is over Envoy's
// buffered limit. In BUFFERED_PARTIAL mode, it won't fail, but won't add anything
// to the response either.
func processEchoHashBuffered(stream extproc.ExternalProcessor_ProcessServer,
	mode extproc_cfg.ProcessingMode_BodySendMode) error {
	logger.Debugf("processEchoHashBuffered. mode = %s", mode)
	// We already have the request headers, so first send back the request body
	// message.
	logger.Debug("Setting target URL to /echo")
	err := stream.Send(&extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_RequestHeaders{
			RequestHeaders: &extproc.HeadersResponse{
				Response: &extproc.CommonResponse{
					HeaderMutation: &extproc.HeaderMutation{
						SetHeaders: []*core.HeaderValueOption{
							{
								Header: &core.HeaderValue{
									Key:      ":path",
									RawValue: []byte("/echo"),
								},
							},
						},
					},
				},
			},
		},
		ModeOverride: &extproc_cfg.ProcessingMode{
			RequestBodyMode:  mode,
			ResponseBodyMode: mode,
		},
	})
	if err != nil {
		return err
	}

	// Even in buffered mode, it's theoretically possible that we'll get
	// the response body before the request body, so use a loop here just
	// like in the (otherwise more complicated) streaming example.
	requestBodyDone := false
	responseBodyDone := false
	requestHash := sha256.New()
	responseHash := sha256.New()

	for !requestBodyDone || !responseBodyDone {
		msg, err := stream.Recv()
		if err == io.EOF {
			return nil
		} else if err != nil {
			logger.Warnf("Error receiving from stream: %v", err)
			return err
		}

		if msg.GetRequestBody() != nil {
			bod := msg.GetRequestBody()
			logger.Debugf("Request body: %d bytes. End = %v",
				len(bod.Body), bod.EndOfStream)
			if !bod.EndOfStream {
				logger.Warn("Expected to receive a buffered request body")
				return nil
			}
			requestHash.Write(bod.Body)
			requestBodyDone = true
			stream.Send(&extproc.ProcessingResponse{
				Response: &extproc.ProcessingResponse_RequestBody{},
			})

		} else if msg.GetResponseBody() != nil {
			bod := msg.GetResponseBody()
			logger.Debugf("Response body: %d bytes. End = %v",
				len(bod.Body), bod.EndOfStream)
			if !bod.EndOfStream {
				logger.Warn("Expected to receive a buffered response body")
				return nil
			}
			responseHash.Write(bod.Body)
			responseBodyDone = true
			stream.Send(&extproc.ProcessingResponse{
				Response: &extproc.ProcessingResponse_ResponseBody{
					ResponseBody: &extproc.BodyResponse{Response: &extproc.CommonResponse{
						HeaderMutation: &extproc.HeaderMutation{
							SetHeaders: []*core.HeaderValueOption{
								{
									Header: &core.HeaderValue{
										Key:      "x-request-body-hash",
										RawValue: []byte(hex.EncodeToString(requestHash.Sum(nil))),
									},
								},
								{
									Header: &core.HeaderValue{
										Key:      "x-response-body-hash",
										RawValue: []byte(hex.EncodeToString(responseHash.Sum(nil))),
									},
								},
							},
						},
					}},
				},
			})

		} else if msg.GetResponseHeaders() != nil {
			logger.Debug("Got response headers")
			stream.Send(&extproc.ProcessingResponse{
				Response: &extproc.ProcessingResponse_ResponseHeaders{},
			})

		} else {
			logger.Debug("Got an unknown message")
		}
	}

	return nil
}

// This example base-64-encodes the HTTP response body. It does it using streaming
// mode so it should work with a body of any size.
func processEncodeDecode(stream extproc.ExternalProcessor_ProcessServer) error {
	logger.Debug("Setting target URL to /echo")
	// We already have the request headers, so first send back the request body
	// message.
	err := stream.Send(&extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_RequestHeaders{
			RequestHeaders: &extproc.HeadersResponse{
				Response: &extproc.CommonResponse{
					HeaderMutation: &extproc.HeaderMutation{
						SetHeaders: []*core.HeaderValueOption{
							{
								Header: &core.HeaderValue{
									Key:      ":path",
									RawValue: []byte("/echo"),
								},
							},
						},
					},
				},
			},
		},
		ModeOverride: &extproc_cfg.ProcessingMode{
			ResponseBodyMode: extproc_cfg.ProcessingMode_STREAMED,
		},
	})
	if err != nil {
		return err
	}

	// We now expect the response headers.
	msg, err := stream.Recv()
	if err == io.EOF {
		return nil
	} else if err != nil {
		logger.Warnf("Error receiving from stream: %v", err)
		return err
	}

	if msg.GetResponseHeaders() == nil {
		// Didn't expect this
		return errors.New("Received unexpected response")
	}
	logger.Debug("Got response headers and removing content-length")

	err = stream.Send(&extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_ResponseHeaders{
			ResponseHeaders: &extproc.HeadersResponse{
				Response: &extproc.CommonResponse{
					HeaderMutation: &extproc.HeaderMutation{
						// Since we're going to modify the body chunks, we have to make sure
						// that there is no content-length header present, since we'll be
						// changing it!
						RemoveHeaders: []string{"content-length"},
					},
				},
			},
		},
	})
	if err != nil {
		return err
	}

	encBuf := &bytes.Buffer{}
	encoder := base64.NewEncoder(base64.StdEncoding, encBuf)
	bodyDone := false

	for !bodyDone {
		msg, err := stream.Recv()
		if err == io.EOF {
			return nil
		} else if err != nil {
			logger.Warnf("Error receiving from stream: %v", err)
			return err
		}

		if msg.GetResponseBody() != nil {
			bod := msg.GetResponseBody()
			logger.Debugf("Request: Encode %d", len(bod.Body))
			encoder.Write(bod.Body)
			if bod.EndOfStream {
				logger.Debugf("Request: EOF")
				bodyDone = true
				encoder.Close()
			}
			decoded := make([]byte, encBuf.Len())
			encBuf.Read(decoded)
			stream.Send(&extproc.ProcessingResponse{
				Response: &extproc.ProcessingResponse_ResponseBody{
					ResponseBody: &extproc.BodyResponse{
						Response: &extproc.CommonResponse{
							BodyMutation: &extproc.BodyMutation{
								Mutation: &extproc.BodyMutation_Body{
									Body: decoded,
								},
							},
						},
					},
				},
			})

		} else {
			logger.Debug("Got an unknown message")
		}
	}

	return nil
}

// This function assembles an "immediate response" message, which will
// return an error directly to the downstream
func sendImmediateResponse(stream extproc.ExternalProcessor_ProcessServer,
	status envoy_type.StatusCode, message, details string) error {
	immediateResponse := &extproc.ImmediateResponse{
		Status: &envoy_type.HttpStatus{
			Code: status,
		},
		Headers: &extproc.HeaderMutation{},
		Body:    message,
		Details: details,
	}
	setHeader(immediateResponse.Headers, "content-type", "text/plain")
	return stream.Send(&extproc.ProcessingResponse{
		Response: &extproc.ProcessingResponse_ImmediateResponse{
			ImmediateResponse: immediateResponse,
		},
	})
}

// This function adds a header to an existing "HeaderMutation" message
func setHeader(mutation *extproc.HeaderMutation, name, value string) {
	mutation.SetHeaders = append(mutation.SetHeaders, &core.HeaderValueOption{
		Header: &core.HeaderValue{
			Key:      name,
			RawValue: []byte(value),
		},
	})
}

// getHeaderValue returns the value of the first HTTP header in the map that matches.
// We don't expect that we will need to look up many headers, so simply do a linear search. If
// we needed to query many more headers, we'd turn the header map into a "map[string]string",
// or a "map[string][]string" if we wanted to handle multi-value headers.
// Return the empty string if the header is not found.
func getHeaderValue(headers *core.HeaderMap, name string) string {
	for _, h := range headers.Headers {
		if h.Key == name {
			// Support backward compatibility with old Envoy versions
			if h.RawValue == nil {
				return h.Value
			}
			return string(h.RawValue)
		}
	}
	return ""
}