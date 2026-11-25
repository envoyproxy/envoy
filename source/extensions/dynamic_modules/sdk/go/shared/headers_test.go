package shared

import (
	"strings"
	"testing"
)

func TestIsValidHeaderName(t *testing.T) {
	t.Parallel()
	for _, name := range []string{"x-custom", "X-Mixed-Case", ":path", "a!#$%&'*+-.^_`|~0"} {
		if !IsValidHeaderName(name) {
			t.Errorf("IsValidHeaderName(%q) = false, want true", name)
		}
	}
	for _, name := range []string{
		"", ":", "::path", "x:y", "bad name", "x-bad\r", "x-bad\n", "x-bad\x00", "café",
	} {
		if IsValidHeaderName(name) {
			t.Errorf("IsValidHeaderName(%q) = true, want false", name)
		}
	}
}

func TestIsValidHeaderValue(t *testing.T) {
	t.Parallel()
	for _, value := range []string{"", "value", "a\tb c", "\x80\xff"} {
		if !IsValidHeaderValue(value) {
			t.Errorf("IsValidHeaderValue(%q) = false, want true", value)
		}
	}
	for _, value := range []string{
		"a\rb", "a\nb", "a\x00b", "bell\x07", "del\x7f", "value\r\nx-injected: 1",
	} {
		if IsValidHeaderValue(value) {
			t.Errorf("IsValidHeaderValue(%q) = true, want false", value)
		}
	}
}

func TestHeaderChecksEveryByte(t *testing.T) {
	t.Parallel()
	for c := range 256 {
		b := byte(c)
		token := 'a' <= b && b <= 'z' || 'A' <= b && b <= 'Z' || '0' <= b && b <= '9' ||
			strings.IndexByte("!#$%&'*+-.^_`|~", b) >= 0
		if got := IsValidHeaderName(string([]byte{b})); got != token {
			t.Errorf("IsValidHeaderName(%q) = %v, want %v", b, got, token)
		}
		allowed := b == '\t' || b >= ' ' && b != 0x7f
		for _, size := range []int{3, 5, 12, 40, 67} {
			for _, position := range []int{0, size / 2, size - 1} {
				value := []byte(strings.Repeat("v", size))
				value[position] = b
				if got := IsValidHeaderValue(string(value)); got != allowed {
					t.Errorf("IsValidHeaderValue(%q at %d of %d) = %v, want %v", b, position, size, got,
						allowed)
				}
			}
		}
	}
}
