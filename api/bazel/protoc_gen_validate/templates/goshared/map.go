package goshared

const mapTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}

	{{ if $r.GetIgnoreEmpty }}
		if len({{ accessor . }}) > 0 {
	{{ end }}

	{{ if $r.GetMinPairs }}
		{{ if eq $r.GetMinPairs $r.GetMaxPairs }}
			if len({{ accessor . }}) != {{ $r.GetMinPairs }} {
				err := {{ err . "value must contain exactly " $r.GetMinPairs " pair(s)" }}
				if !all { return err }
				errors = append(errors, err)
			}
		{{ else if $r.MaxPairs }}
			if l := len({{ accessor . }}); l < {{ $r.GetMinPairs }} || l > {{ $r.GetMaxPairs }} {
				err := {{ err . "value must contain between " $r.GetMinPairs " and " $r.GetMaxPairs " pairs, inclusive" }}
				if !all { return err }
				errors = append(errors, err)
			}
		{{ else }}
			if len({{ accessor . }}) < {{ $r.GetMinPairs }} {
				err := {{ err . "value must contain at least " $r.GetMinPairs " pair(s)" }}
				if !all { return err }
				errors = append(errors, err)
			}
		{{ end }}
	{{ else if $r.MaxPairs }}
		if len({{ accessor . }}) > {{ $r.GetMaxPairs }} {
			err := {{ err . "value must contain no more than " $r.GetMaxPairs " pair(s)" }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}

	{{ if or $r.GetNoSparse (ne (.Elem "" "").Typ "none") (ne (.Key "" "").Typ "none") }}
		{{- /* Sort the keys to make the iteration order (and therefore failure output) deterministic. */ -}}
		{
			sorted_keys := make([]{{ (typ .Field).Key }}, len({{ accessor . }}))
			i := 0
			for key := range {{ accessor . }} {
				sorted_keys[i] = key
				i++
			}
			sort.Slice(sorted_keys, func (i, j int) bool { return sorted_keys[i] < sorted_keys[j] })
			for _, key := range sorted_keys {
				val := {{ accessor .}}[key]
				_ = val

				{{ if $r.GetNoSparse }}
					if val == nil {
						err := {{ errIdx . "key" "value cannot be sparse, all pairs must be non-nil" }}
						if !all { return err }
						errors = append(errors, err)
					}
				{{ end }}

				{{ render (.Key "key" "key") }}

				{{ render (.Elem "val" "key") }}
			}
		}
	{{ end }}

	{{ if $r.GetIgnoreEmpty }}
		}
	{{ end }}

`
