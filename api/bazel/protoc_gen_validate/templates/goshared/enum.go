package goshared

const enumTpl = `
		{{ $f := .Field }}{{ $r := .Rules }}
		{{ template "const" . }}
		{{ template "in" . }}

		{{ if $r.GetDefinedOnly }}
			if _, ok := {{ (typ $f).Element.Value }}_name[int32({{ accessor . }})]; !ok {
				err := {{ err . "value must be one of the defined enum values" }}
				if !all { return err }
				errors = append(errors, err)
			}
		{{ end }}
`
