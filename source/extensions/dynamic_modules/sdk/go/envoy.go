package envoy

// OnProgramInit is a function that is called when the program is loaded.
// The function should return 0 on success, and non-zero on failure.
//
// This is useful to check process-wide platform compatibility such as
// checking if a specific CPU feature is available.
//
// When non-zero is returned, the program will not be loaded by Envoy,
// and the filter will not be initialized.
var OnProgramInit func() int
