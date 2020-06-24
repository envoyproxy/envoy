package v2

import (
	"context"
	"log"
	"strings"

	envoy_api_v2_core "github.com/envoyproxy/go-control-plane/envoy/api/v2/core"
	envoy_service_auth_v2 "github.com/envoyproxy/go-control-plane/envoy/service/auth/v2"
	"github.com/golang/protobuf/ptypes/wrappers"
	"google.golang.org/genproto/googleapis/rpc/code"
	"google.golang.org/genproto/googleapis/rpc/status"

	"github.com/envoyproxy/envoy/examples/ext_authz/auth/grpc-service/pkg/auth"
)

type server struct {
	users auth.Users
}

var _ envoy_service_auth_v2.AuthorizationServer = &server{}

// New creates a new authorization server.
func New(users auth.Users) envoy_service_auth_v2.AuthorizationServer {
	return &server{users}
}

// Check implements authorization's Check interface which performs authorization check based on the
// attributes associated with the incoming request.
func (s *server) Check(
	ctx context.Context,
	req *envoy_service_auth_v2.CheckRequest) (*envoy_service_auth_v2.CheckResponse, error) {
	authorization := req.Attributes.Request.Http.Headers["authorization"]
	log.Println(authorization)

	extracted := strings.Fields(authorization)
	if len(extracted) == 2 && extracted[0] == "Bearer" {
		valid, user := s.users.Check(extracted[1])
		if valid {
			return &envoy_service_auth_v2.CheckResponse{
				HttpResponse: &envoy_service_auth_v2.CheckResponse_OkResponse{
					OkResponse: &envoy_service_auth_v2.OkHttpResponse{
						Headers: []*envoy_api_v2_core.HeaderValueOption{
							{
								Append: &wrappers.BoolValue{Value: false},
								Header: &envoy_api_v2_core.HeaderValue{
									// For a successful request, the authorization server sets the
									// x-current-user value.
									Key:   "x-current-user",
									Value: user,
								},
							},
						},
					},
				},
				Status: &status.Status{
					Code: int32(code.Code_OK),
				},
			}, nil
		}
	}

	return &envoy_service_auth_v2.CheckResponse{
		Status: &status.Status{
			Code: int32(code.Code_PERMISSION_DENIED),
		},
	}, nil
}
