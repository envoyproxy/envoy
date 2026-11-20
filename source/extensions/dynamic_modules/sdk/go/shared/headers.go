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
	for i := 0; i < len(name); i++ {
		if !isTokenChar(name[i]) {
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
	for i := 0; i < len(value); i++ {
		if c := value[i]; c != '\t' && (c < ' ' || c == 0x7f) {
			return false
		}
	}
	return true
}

func isTokenChar(c byte) bool {
	if ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9') {
		return true
	}
	return strings.IndexByte("!#$%&'*+-.^_`|~", c) >= 0
}
