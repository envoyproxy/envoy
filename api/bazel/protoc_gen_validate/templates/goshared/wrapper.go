package goshared

const wrapperTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}

	if wrapper := {{ accessor . }}; wrapper != nil {
		{{ render (unwrap . "wrapper") }}
	} {{ if .MessageRules.GetRequired }} else {
		err := {{ err . "value is required and must not be nil." }}
		if !all { return err }
		errors = append(errors, err)
	} {{ end }}
`
