package golang

const durationTpl = `{{ $f := .Field }}{{ $r := .Rules }}
	{{ template "required" . }}

	{{ if or $r.In $r.NotIn $r.Lt $r.Lte $r.Gt $r.Gte $r.Const }}
		if d := {{ accessor . }}; d != nil {
			dur, err := d.AsDuration(), d.CheckValid()
			if err != nil {
				err = {{ errCause . "err" "value is not a valid duration" }}
				if !all { return err }
				errors = append(errors, err)
			} else {
				{{ template "durationcmp" . }}
			}
		}
	{{ end }}
`
