package buffer

import (
	"testing"
)

// snprintfFiller returns a fill function that mimics the runtime's snprintf-style writer for a
// fixed value. It writes min(size, cap(buf)) bytes into the backing array and reports the full
// size. It records the number of invocations so tests can assert grow-and-retry convergence.
func snprintfFiller(value string, exists bool, calls *int) func([]byte) (uint64, bool) {
	return func(buf []byte) (uint64, bool) {
		*calls++
		if !exists {
			return 0, false
		}
		copy(buf[:cap(buf)], value)
		return uint64(len(value)), true
	}
}

func TestFillGrowsAndRetriesFromZeroCapacity(t *testing.T) {
	calls := 0
	// A zero-capacity buffer forces a length query followed by a single grow-and-retry.
	got, ok := Fill(make([]byte, 0), snprintfFiller("hello.world", true, &calls))
	if !ok {
		t.Fatal("expected ok")
	}
	if string(got) != "hello.world" {
		t.Errorf("expected %q, got %q", "hello.world", string(got))
	}
	if cap(got) < len("hello.world") {
		t.Errorf("expected capacity >= %d, got %d", len("hello.world"), cap(got))
	}
	if calls != 2 {
		t.Errorf("expected 2 fill calls (length query + write), got %d", calls)
	}
}

func TestFillGrowsAndRetriesFromTooSmallCapacity(t *testing.T) {
	calls := 0
	// A non-zero but too-small buffer exercises the partial-write-then-discard-and-grow path.
	got, ok := Fill(make([]byte, 0, 3), snprintfFiller("metric", true, &calls))
	if !ok || string(got) != "metric" {
		t.Fatalf("expected %q, got %q ok=%v", "metric", string(got), ok)
	}
	if cap(got) < len("metric") {
		t.Errorf("expected capacity >= %d, got %d", len("metric"), cap(got))
	}
	if calls != 2 {
		t.Errorf("expected 2 fill calls (truncated + retry), got %d", calls)
	}
}

func TestFillExactFit(t *testing.T) {
	calls := 0
	got, ok := Fill(make([]byte, 0, len("metric")), snprintfFiller("metric", true, &calls))
	if !ok || string(got) != "metric" {
		t.Fatalf("expected %q, got %q ok=%v", "metric", string(got), ok)
	}
	if calls != 1 {
		t.Errorf("expected 1 fill call, got %d", calls)
	}
}

func TestFillRoomToSpare(t *testing.T) {
	calls := 0
	got, ok := Fill(make([]byte, 0, 64), snprintfFiller("short", true, &calls))
	if !ok || string(got) != "short" {
		t.Fatalf("expected %q, got %q ok=%v", "short", string(got), ok)
	}
	if len(got) != len("short") {
		t.Errorf("expected len %d, got %d", len("short"), len(got))
	}
	if calls != 1 {
		t.Errorf("expected 1 fill call, got %d", calls)
	}
}

func TestFillNotFoundReturnsBufferUnchanged(t *testing.T) {
	calls := 0
	got, ok := Fill(make([]byte, 0, 8), snprintfFiller("", false, &calls))
	if ok {
		t.Fatal("expected not found")
	}
	if len(got) != 0 || cap(got) != 8 {
		t.Errorf("expected buffer unchanged (len 0 cap 8), got len %d cap %d", len(got), cap(got))
	}
	if calls != 1 {
		t.Errorf("expected 1 fill call, got %d", calls)
	}
}

func TestFillReusesAllocationAcrossCalls(t *testing.T) {
	calls := 0
	buf, ok := Fill(make([]byte, 0), snprintfFiller("a.very.long.metric.name", true, &calls))
	if !ok {
		t.Fatal("expected ok")
	}
	grownCap := cap(buf)

	// Reusing the grown buffer for a shorter value must not reallocate or retry.
	calls = 0
	got, ok := Fill(buf[:0], snprintfFiller("short", true, &calls))
	if !ok || string(got) != "short" {
		t.Fatalf("expected %q, got %q ok=%v", "short", string(got), ok)
	}
	if cap(got) != grownCap {
		t.Errorf("expected reuse of capacity %d, got %d", grownCap, cap(got))
	}
	if calls != 1 {
		t.Errorf("expected 1 fill call on reuse, got %d", calls)
	}
}

func TestFillEmptyValue(t *testing.T) {
	calls := 0
	got, ok := Fill(make([]byte, 0), snprintfFiller("", true, &calls))
	if !ok {
		t.Fatal("expected ok")
	}
	if len(got) != 0 {
		t.Errorf("expected empty result, got %q", string(got))
	}
	if calls != 1 {
		t.Errorf("expected 1 fill call for empty value, got %d", calls)
	}
}

// snprintfFillerTwo mimics the dual-buffer text-readout getter. It writes both the name and value,
// min(size, capacity) bytes each, on every call and reports both full sizes.
func snprintfFillerTwo(name, value string, exists bool, calls *int) func([]byte, []byte) (uint64, uint64, bool) {
	return func(nameBuf, valueBuf []byte) (uint64, uint64, bool) {
		*calls++
		if !exists {
			return 0, 0, false
		}
		copy(nameBuf[:cap(nameBuf)], name)
		copy(valueBuf[:cap(valueBuf)], value)
		return uint64(len(name)), uint64(len(value)), true
	}
}

func TestFillTwoBothFit(t *testing.T) {
	calls := 0
	name, value, ok := FillTwo(make([]byte, 0, 32), make([]byte, 0, 32),
		snprintfFillerTwo("pilot.version", "1.28.0", true, &calls))
	if !ok || string(name) != "pilot.version" || string(value) != "1.28.0" {
		t.Fatalf("expected pilot.version/1.28.0, got %q/%q ok=%v", string(name), string(value), ok)
	}
	if calls != 1 {
		t.Errorf("expected 1 fill call, got %d", calls)
	}
}

func TestFillTwoNameGrows(t *testing.T) {
	calls := 0
	// Name starts too small at cap 0 while the value already fits, so only the name must grow.
	name, value, ok := FillTwo(make([]byte, 0), make([]byte, 0, 32),
		snprintfFillerTwo("pilot.version", "1.28.0", true, &calls))
	if !ok || string(name) != "pilot.version" || string(value) != "1.28.0" {
		t.Fatalf("expected pilot.version/1.28.0, got %q/%q ok=%v", string(name), string(value), ok)
	}
	if calls != 2 {
		t.Errorf("expected 2 fill calls (name grows), got %d", calls)
	}
}

func TestFillTwoValueGrows(t *testing.T) {
	calls := 0
	// Value starts too small at cap 0 while the name already fits, so only the value must grow.
	name, value, ok := FillTwo(make([]byte, 0, 32), make([]byte, 0),
		snprintfFillerTwo("pilot.version", "1.28.0", true, &calls))
	if !ok || string(name) != "pilot.version" || string(value) != "1.28.0" {
		t.Fatalf("expected pilot.version/1.28.0, got %q/%q ok=%v", string(name), string(value), ok)
	}
	if calls != 2 {
		t.Errorf("expected 2 fill calls (value grows), got %d", calls)
	}
}

func TestFillTwoBothGrow(t *testing.T) {
	calls := 0
	// Both buffers start at zero capacity, so both must grow in a single retry.
	name, value, ok := FillTwo(make([]byte, 0), make([]byte, 0),
		snprintfFillerTwo("pilot.version", "1.28.0", true, &calls))
	if !ok || string(name) != "pilot.version" || string(value) != "1.28.0" {
		t.Fatalf("expected pilot.version/1.28.0, got %q/%q ok=%v", string(name), string(value), ok)
	}
	if calls != 2 {
		t.Errorf("expected 2 fill calls (both grow), got %d", calls)
	}
}

func TestFillTwoNotFoundReturnsBuffersUnchanged(t *testing.T) {
	calls := 0
	name, value, ok := FillTwo(make([]byte, 0, 8), make([]byte, 0, 8),
		snprintfFillerTwo("", "", false, &calls))
	if ok {
		t.Fatal("expected not found")
	}
	if len(name) != 0 || cap(name) != 8 || len(value) != 0 || cap(value) != 8 {
		t.Errorf("expected buffers unchanged, got name(len %d cap %d) value(len %d cap %d)",
			len(name), cap(name), len(value), cap(value))
	}
	if calls != 1 {
		t.Errorf("expected 1 fill call, got %d", calls)
	}
}
