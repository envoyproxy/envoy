#pragma once

namespace http2 {
namespace test {

void MarkMemoryUninitialized(void* ptr, size_t num_bytes) {}
void MarkMemoryUninitialized(void* ptr, size_t num_bytes, Http2Random* rng) {}

template <class T>
void MarkObjectUninitialized(T* ptr) {}
template <class T>
void MarkObjectUninitialized(T* ptr, Http2Random* rng) {}

template <class T, size_t N>
void MarkArrayUninitialized(T (&array)[N]) {}
template <class T, size_t N>
void MarkArrayUninitialized(T (&array)[N], Http2Random* rng) {}

template <class T, class... Args>
void Http2ReconstructObjectImpl(T* ptr, Http2Random* rng, Args&&... args) {}
template <class T>
void Http2DefaultReconstructObjectImpl(T* ptr, Http2Random* rng) {}

}  // namespace test
}  // namespace http2
