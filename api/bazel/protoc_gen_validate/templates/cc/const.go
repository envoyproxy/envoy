package cc

const constTpl = `{{ $f := .Field }}{{ $r := .Rules }}
	{{ if $r.Const }}
		if ({{ accessor . }} != {{ lit $r.GetConst }}) {
			{{- if isEnum $f }}
			{{ err . "value must equal " (enumVal $f $r.GetConst) }}
			{{- else }}
			{{ err . "value must equal " (lit $r.GetConst) }}
			{{- end }}
		}
	{{ end }}
`
