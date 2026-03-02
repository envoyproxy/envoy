package utility

import (
	"testing"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared/fake"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared/mocks"
	gomock "go.uber.org/mock/gomock"
)

func TestReadWholeRequestBody_SameChunks(t *testing.T) {
	// When received body and buffered body point to the same underlying memory,
	// the data should only appear once in the result.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	body := []byte("hello world")
	buf := fake.NewFakeBodyBuffer(body)

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedRequestBody().Return(buf)
	handle.EXPECT().ReceivedRequestBody().Return(buf) // same pointer → same chunks

	result := ReadWholeRequestBody(handle)
	if string(result) != "hello world" {
		t.Errorf("expected %q, got %q", "hello world", string(result))
	}
}

func TestReadWholeRequestBody_DifferentChunks(t *testing.T) {
	// When buffered and received body point to different memory, both are combined.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	buffered := fake.NewFakeBodyBuffer([]byte("hello "))
	received := fake.NewFakeBodyBuffer([]byte("world"))

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedRequestBody().Return(buffered)
	handle.EXPECT().ReceivedRequestBody().Return(received)

	result := ReadWholeRequestBody(handle)
	if string(result) != "hello world" {
		t.Errorf("expected %q, got %q", "hello world", string(result))
	}
}

func TestReadWholeRequestBody_EmptyBuffered(t *testing.T) {
	// When buffered body is empty, result equals the received body.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	buffered := fake.NewFakeBodyBuffer([]byte{})
	received := fake.NewFakeBodyBuffer([]byte("world"))

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedRequestBody().Return(buffered)
	handle.EXPECT().ReceivedRequestBody().Return(received)

	result := ReadWholeRequestBody(handle)
	if string(result) != "world" {
		t.Errorf("expected %q, got %q", "world", string(result))
	}
}

func TestReadWholeResponseBody_SameChunks(t *testing.T) {
	// When received body and buffered body point to the same underlying memory,
	// the data should only appear once in the result.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	body := []byte("hello world")
	buf := fake.NewFakeBodyBuffer(body)

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedResponseBody().Return(buf)
	handle.EXPECT().ReceivedResponseBody().Return(buf) // same pointer → same chunks

	result := ReadWholeResponseBody(handle)
	if string(result) != "hello world" {
		t.Errorf("expected %q, got %q", "hello world", string(result))
	}
}

func TestReadWholeResponseBody_DifferentChunks(t *testing.T) {
	// When buffered and received body point to different memory, both are combined.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	buffered := fake.NewFakeBodyBuffer([]byte("hello "))
	received := fake.NewFakeBodyBuffer([]byte("world"))

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedResponseBody().Return(buffered)
	handle.EXPECT().ReceivedResponseBody().Return(received)

	result := ReadWholeResponseBody(handle)
	if string(result) != "hello world" {
		t.Errorf("expected %q, got %q", "hello world", string(result))
	}
}

func TestReadWholeResponseBody_EmptyBuffered(t *testing.T) {
	// When buffered body is empty, result equals the received body.
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	buffered := fake.NewFakeBodyBuffer([]byte{})
	received := fake.NewFakeBodyBuffer([]byte("world"))

	handle := mocks.NewMockHttpFilterHandle(ctrl)
	handle.EXPECT().BufferedResponseBody().Return(buffered)
	handle.EXPECT().ReceivedResponseBody().Return(received)

	result := ReadWholeResponseBody(handle)
	if string(result) != "world" {
		t.Errorf("expected %q, got %q", "world", string(result))
	}
}
