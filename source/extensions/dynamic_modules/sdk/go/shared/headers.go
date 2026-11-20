package shared

import "strings"

// IsValidHeaderName reports whether name is a header name that Envoy accepts: a non-empty HTTP
// token, optionally prefixed with ':' for a pseudo-header. Upper case letters are allowed.
//
// Envoy does not check the header names that a module passes, so a module that builds a name
// from request data or other untrusted input should check it with this first.
func IsValidHeaderName(name string) bool {
	name = strings.TrimPrefix(name, ":")
	if name == "" {
		return false
	}
	for i := range len(name) {
		if !tokenChars[name[i]] {
			return false
		}
	}
	return true
}

// IsValidHeaderValue reports whether value is a header value that Envoy accepts: one without a
// control character other than horizontal tab. Bytes above 0x7f are allowed.
//
// Envoy does not check the header values that a module passes. A CR or LF in a value can end the
// header and start another on an HTTP/1 connection, so a module that copies request data or
// other untrusted input into a header should check it with this first.
func IsValidHeaderValue(value string) bool {
	for i := range len(value) {
		if !valueChars[value[i]] {
			return false
		}
	}
	return true
}

// tokenChars holds the bytes of an HTTP token (RFC 9110), which is what a header name is, and
// valueChars the bytes allowed in a header value.
var tokenChars, valueChars = func() (token, value [256]bool) {
	for c := range 256 {
		b := byte(c)
		token[c] = 'a' <= b && b <= 'z' || 'A' <= b && b <= 'Z' || '0' <= b && b <= '9' ||
			strings.IndexByte("!#$%&'*+-.^_`|~", b) >= 0
		value[c] = b == '\t' || b >= ' ' && b != 0x7f
	}
	return token, value
}()
