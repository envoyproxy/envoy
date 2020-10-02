// Envoy snapshot of Chromium URL path normalization, see README.md.
// NOLINT(namespace-envoy)

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_CANON_H_
#define URL_URL_CANON_H_

#include <stdlib.h>
#include <string.h>

#include "common/chromium_url/envoy_shim.h"
#include "common/chromium_url/url_parse.h"

namespace chromium_url {

// Canonicalizer output -------------------------------------------------------

// Base class for the canonicalizer output, this maintains a buffer and
// supports simple resizing and append operations on it.
//
// It is VERY IMPORTANT that no virtual function calls be made on the common
// code path. We only have two virtual function calls, the destructor and a
// resize function that is called when the existing buffer is not big enough.
// The derived class is then in charge of setting up our buffer which we will
// manage.
template <typename T> class CanonOutputT {
public:
  CanonOutputT() : buffer_(NULL), buffer_len_(0), cur_len_(0) {}
  virtual ~CanonOutputT() = default;

  // Implemented to resize the buffer. This function should update the buffer
  // pointer to point to the new buffer, and any old data up to |cur_len_| in
  // the buffer must be copied over.
  //
  // The new size |sz| must be larger than buffer_len_.
  virtual void Resize(int sz) = 0;

  // Accessor for returning a character at a given position. The input offset
  // must be in the valid range.
  inline T at(int offset) const { return buffer_[offset]; }

  // Sets the character at the given position. The given position MUST be less
  // than the length().
  inline void set(int offset, T ch) { buffer_[offset] = ch; }

  // Returns the number of characters currently in the buffer.
  inline int length() const { return cur_len_; }

  // Returns the current capacity of the buffer. The length() is the number of
  // characters that have been declared to be written, but the capacity() is
  // the number that can be written without reallocation. If the caller must
  // write many characters at once, it can make sure there is enough capacity,
  // write the data, then use set_size() to declare the new length().
  int capacity() const { return buffer_len_; }

  // Called by the user of this class to get the output. The output will NOT
  // be NULL-terminated. Call length() to get the
  // length.
  const T* data() const { return buffer_; }
  T* data() { return buffer_; }

  // Shortens the URL to the new length. Used for "backing up" when processing
  // relative paths. This can also be used if an external function writes a lot
  // of data to the buffer (when using the "Raw" version below) beyond the end,
  // to declare the new length.
  //
  // This MUST NOT be used to expand the size of the buffer beyond capacity().
  void set_length(int new_len) { cur_len_ = new_len; }

  // This is the most performance critical function, since it is called for
  // every character.
  void push_back(T ch) {
    // In VC2005, putting this common case first speeds up execution
    // dramatically because this branch is predicted as taken.
    if (cur_len_ < buffer_len_) {
      buffer_[cur_len_] = ch;
      cur_len_++;
      return;
    }

    // Grow the buffer to hold at least one more item. Hopefully we won't have
    // to do this very often.
    if (!Grow(1))
      return;

    // Actually do the insertion.
    buffer_[cur_len_] = ch;
    cur_len_++;
  }

  // Appends the given string to the output.
  void Append(const T* str, int str_len) {
    if (cur_len_ + str_len > buffer_len_) {
      if (!Grow(cur_len_ + str_len - buffer_len_))
        return;
    }
    for (int i = 0; i < str_len; i++)
      buffer_[cur_len_ + i] = str[i];
    cur_len_ += str_len;
  }

  void ReserveSizeIfNeeded(int estimated_size) {
    // Reserve a bit extra to account for escaped chars.
    if (estimated_size > buffer_len_)
      Resize(estimated_size + 8);
  }

protected:
  // Grows the given buffer so that it can fit at least |min_additional|
  // characters. Returns true if the buffer could be resized, false on OOM.
  bool Grow(int min_additional) {
    static const int kMinBufferLen = 16;
    int new_len = (buffer_len_ == 0) ? kMinBufferLen : buffer_len_;
    do {
      if (new_len >= (1 << 30)) // Prevent overflow below.
        return false;
      new_len *= 2;
    } while (new_len < buffer_len_ + min_additional);
    Resize(new_len);
    return true;
  }

  T* buffer_;
  int buffer_len_;

  // Used characters in the buffer.
  int cur_len_;
};

// Simple implementation of the CanonOutput using new[]. This class
// also supports a static buffer so if it is allocated on the stack, most
// URLs can be canonicalized with no heap allocations.
template <typename T, int fixed_capacity = 1024> class RawCanonOutputT : public CanonOutputT<T> {
public:
  RawCanonOutputT() : CanonOutputT<T>() {
    this->buffer_ = fixed_buffer_;
    this->buffer_len_ = fixed_capacity;
  }
  ~RawCanonOutputT() override {
    if (this->buffer_ != fixed_buffer_)
      delete[] this->buffer_;
  }

  void Resize(int sz) override {
    T* new_buf = new T[sz];
    memcpy(new_buf, this->buffer_, sizeof(T) * (this->cur_len_ < sz ? this->cur_len_ : sz));
    if (this->buffer_ != fixed_buffer_)
      delete[] this->buffer_;
    this->buffer_ = new_buf;
    this->buffer_len_ = sz;
  }

protected:
  T fixed_buffer_[fixed_capacity];
};

// Explicitly instantiate commonly used instantiations.
extern template class EXPORT_TEMPLATE_DECLARE(COMPONENT_EXPORT(URL)) CanonOutputT<char>;

// Normally, all canonicalization output is in narrow characters. We support
// the templates so it can also be used internally if a wide buffer is
// required.
using CanonOutput = CanonOutputT<char>;

template <int fixed_capacity>
class RawCanonOutput : public RawCanonOutputT<char, fixed_capacity> {};

// Path. If the input does not begin in a slash (including if the input is
// empty), we'll prepend a slash to the path to make it canonical.
//
// The 8-bit version assumes UTF-8 encoding, but does not verify the validity
// of the UTF-8 (i.e., you can have invalid UTF-8 sequences, invalid
// characters, etc.). Normally, URLs will come in as UTF-16, so this isn't
// an issue. Somebody giving us an 8-bit path is responsible for generating
// the path that the server expects (we'll escape high-bit characters), so
// if something is invalid, it's their problem.
COMPONENT_EXPORT(URL)
bool CanonicalizePath(const char* spec, const Component& path, CanonOutput* output,
                      Component* out_path);

} // namespace chromium_url

#endif // URL_URL_CANON_H_
