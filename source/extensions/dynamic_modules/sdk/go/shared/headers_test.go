package shared

import "testing"

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
