package gosdk

// OnProgramInit is a function that can be set by the user in a Go package init function.
// This will be called when the dynamic module is initialized. The function should return true
// if initialization was successful, and false otherwise. When false is returned,
// the module will not be loaded. This is useful for performing any process-wide initialization
// and compatibility checks.
//
// This defaults to a function that always returns true.
var OnProgramInit = func() bool { return true }
