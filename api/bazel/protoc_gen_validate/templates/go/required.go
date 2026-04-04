package golang

const requiredTpl = `
	{{ if .Rules.GetRequired }}
		if {{ accessor . }} == nil {
			err := {{ err . "value is required" }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}
`
