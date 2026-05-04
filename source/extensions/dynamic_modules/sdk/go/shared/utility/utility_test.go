package utility

import (
	"testing"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared/fake"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared/mocks"
	gomock "go.uber.org/mock/gomock"
)

func TestReadWholeRequestBody_ReceivedIsBuffered(t *testing.T) {
	// When ReceivedBufferedRequestBody() returns true (previous filter did StopAndBuffer and
	// resumed), only the buffered body should be read to avoid duplicating data.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	body := []byte("hello world")
	buf := fake.NewFakeBodyBuffer(body)

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedRequestBody().Return(buf)
	handle.EXPECT().ReceivedRequestBody().Return(buf)
	handle.EXPECT().ReceivedBufferedRequestBody().Return(true)

	result := ReadWholeRequestBody(handle)
	if string(result) != "hello world" {
		t.Errorf("expected %q, got %q", "hello world", string(result))
	}
}

func TestReadWholeRequestBody_DifferentChunks(t *testing.T) {
	// When ReceivedBufferedRequestBody() returns false, both buffered and received are combined.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	buffered := fake.NewFakeBodyBuffer([]byte("hello "))
	received := fake.NewFakeBodyBuffer([]byte("world"))

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedRequestBody().Return(buffered)
	handle.EXPECT().ReceivedBufferedRequestBody().Return(false)
	handle.EXPECT().ReceivedRequestBody().Return(received)

	result := ReadWholeRequestBody(handle)
	if string(result) != "hello world" {
		t.Errorf("expected %q, got %q", "hello world", string(result))
	}
}

func TestReadWholeRequestBody_EmptyBuffered(t *testing.T) {
	// When buffered body is empty and received is not the buffered body, result equals received.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	buffered := fake.NewFakeBodyBuffer([]byte{})
	received := fake.NewFakeBodyBuffer([]byte("world"))

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedRequestBody().Return(buffered)
	handle.EXPECT().ReceivedBufferedRequestBody().Return(false)
	handle.EXPECT().ReceivedRequestBody().Return(received)

	result := ReadWholeRequestBody(handle)
	if string(result) != "world" {
		t.Errorf("expected %q, got %q", "world", string(result))
	}
}

func TestReadWholeResponseBody_ReceivedIsBuffered(t *testing.T) {
	// When ReceivedBufferedResponseBody() returns true, only the buffered body should be read.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	body := []byte("hello world")
	buf := fake.NewFakeBodyBuffer(body)

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedResponseBody().Return(buf)
	handle.EXPECT().ReceivedResponseBody().Return(buf)
	handle.EXPECT().ReceivedBufferedResponseBody().Return(true)

	result := ReadWholeResponseBody(handle)
	if string(result) != "hello world" {
		t.Errorf("expected %q, got %q", "hello world", string(result))
	}
}

func TestReadWholeResponseBody_DifferentChunks(t *testing.T) {
	// When ReceivedBufferedResponseBody() returns false, both buffered and received are combined.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	buffered := fake.NewFakeBodyBuffer([]byte("hello "))
	received := fake.NewFakeBodyBuffer([]byte("world"))

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedResponseBody().Return(buffered)
	handle.EXPECT().ReceivedBufferedResponseBody().Return(false)
	handle.EXPECT().ReceivedResponseBody().Return(received)

	result := ReadWholeResponseBody(handle)
	if string(result) != "hello world" {
		t.Errorf("expected %q, got %q", "hello world", string(result))
	}
}

func TestReadWholeResponseBody_EmptyBuffered(t *testing.T) {
	// When buffered body is empty and received is not the buffered body, result equals received.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	buffered := fake.NewFakeBodyBuffer([]byte{})
	received := fake.NewFakeBodyBuffer([]byte("world"))

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedResponseBody().Return(buffered)
	handle.EXPECT().ReceivedBufferedResponseBody().Return(false)
	handle.EXPECT().ReceivedResponseBody().Return(received)

	result := ReadWholeResponseBody(handle)
	if string(result) != "world" {
		t.Errorf("expected %q, got %q", "world", string(result))
	}
}

func TestRefreshRouteCluster_MockIsCalled(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().RefreshRouteCluster().Times(1)
	handle.RefreshRouteCluster()
}

func TestClearRouteCache_MockIsCalled(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().ClearRouteCache().Times(1)
	handle.ClearRouteCache()
}

func TestGetDynamicMetadata(t *testing.T) {
	for _, tt := range []struct {
		name     string
		filter   string
		path     string
		value    string
		expectOk bool
	}{
		{"missing namespace", "no_such_filter", "method", "", false},
		{"flat string key", "mcp_filter", "method", "tools/call", true},
		{"dotted path", "mcp_filter", "params.protocolVersion", "2025-11-25", true},
	} {
		t.Run(tt.name, func(t *testing.T) {
			ctrl := gomock.NewController(t)
			defer ctrl.Finish()

			var retBuf shared.UnsafeEnvoyBuffer
			if tt.expectOk {
				retBuf = fake.NewFakeBodyBuffer([]byte(tt.value)).GetChunks()[0]
			}

			handle := mocks.NewMockHttpFilterHandle(ctrl)
			handle.EXPECT().GetDynamicMetadata(tt.filter, tt.path).Return(retBuf, tt.expectOk)

			result, ok := handle.GetDynamicMetadata(tt.filter, tt.path)
			if ok != tt.expectOk {
				t.Fatalf("got ok=%v, want %v", ok, tt.expectOk)
			}
			if ok && result.Len != uint64(len(tt.value)) {
				t.Errorf("got len=%d, want %d", result.Len, len(tt.value))
			}
		})
	}
}
