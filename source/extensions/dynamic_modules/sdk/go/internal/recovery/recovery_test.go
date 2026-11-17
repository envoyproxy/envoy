package recovery

import "testing"

// callGuarded runs body under Export and returns failClosed if body panics, mirroring how a
// value-returning export defers the barrier.
func callGuarded[T comparable](failClosed T, body func() T) (ret T) {
	defer Export("test_export", failClosed, &ret)
	return body()
}

// callGuardedVoid runs body under ExportVoid, mirroring how a void-returning export defers it.
func callGuardedVoid(body func()) {
	defer ExportVoid("test_export_void")
	body()
}

func TestExportReturnsBodyValueWhenNoPanic(t *testing.T) {
	if got := callGuarded(42, func() int { return 7 }); got != 7 {
		t.Fatalf("got %d, want 7", got)
	}
}

func TestExportReturnsFailClosedOnPanic(t *testing.T) {
	if got := callGuarded(42, func() int { panic("boom") }); got != 42 {
		t.Fatalf("got %d, want 42", got)
	}
}

func TestExportReturnsNilPointerOnPanic(t *testing.T) {
	if got := callGuarded[*int](nil, func() *int { panic("boom") }); got != nil {
		t.Fatalf("got %v, want nil", got)
	}
}

func TestExportReturnsBoolFailClosedOnPanic(t *testing.T) {
	if got := callGuarded(true, func() bool { panic("boom") }); !got {
		t.Fatal("got false, want true")
	}
}

func TestExportLogsRecoveredPanic(t *testing.T) {
	var gotName string
	var gotRecovered any
	prev := Logger
	Logger = func(functionName string, recovered any) {
		gotName = functionName
		gotRecovered = recovered
	}
	defer func() { Logger = prev }()

	_ = callGuarded(0, func() int { panic("kaboom") })
	if gotName != "test_export" {
		t.Errorf("function name = %q, want test_export", gotName)
	}
	if gotRecovered != "kaboom" {
		t.Errorf("recovered = %v, want kaboom", gotRecovered)
	}
}

func TestExportVoidSwallowsPanicAndLogs(t *testing.T) {
	var logged bool
	prev := Logger
	Logger = func(string, any) { logged = true }
	defer func() { Logger = prev }()

	// If the panic were not recovered, this would crash the test binary rather than return.
	callGuardedVoid(func() { panic("boom") })
	if !logged {
		t.Fatal("Logger was not called on a recovered panic")
	}
}

func TestExportDoesNothingWithoutPanic(t *testing.T) {
	var logged bool
	prev := Logger
	Logger = func(string, any) { logged = true }
	defer func() { Logger = prev }()

	_ = callGuarded(0, func() int { return 7 })
	if logged {
		t.Fatal("Logger was called without a panic")
	}
}

func TestExportVoidDoesNothingWithoutPanic(t *testing.T) {
	var logged bool
	prev := Logger
	Logger = func(string, any) { logged = true }
	defer func() { Logger = prev }()

	callGuardedVoid(func() {})
	if logged {
		t.Fatal("Logger was called without a panic")
	}
}
