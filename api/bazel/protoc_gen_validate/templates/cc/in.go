package cc

const inTpl = `{{ $f := .Field -}}{{ $r := .Rules -}}
	{{- if $r.In }}
		if ({{ lookup $f "InLookup" }}.find(static_cast<decltype({{ lookup $f "InLookup" }})::key_type>({{ accessor . }})) == {{ lookup $f "InLookup" }}.end()) {
			{{- if isEnum $f }}
			{{ err . "value must be in list " (enumList $f $r.In) }}
			{{- else }}
			{{ err . "value must be in list " $r.In }}
			{{- end }}
		}
	{{- else if $r.NotIn }}
		if ({{ lookup $f "NotInLookup" }}.find(static_cast<decltype({{ lookup $f "NotInLookup" }})::key_type>({{ accessor . }})) != {{ lookup $f "NotInLookup" }}.end()) {
			{{- if isEnum $f }}
			{{ err . "value must not be in list " (enumList $f $r.NotIn) }}
			{{- else }}
			{{ err . "value must not be in list " $r.NotIn }}
			{{- end }}
		}
	{{- end }}
`
