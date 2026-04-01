package goshared

const msgTpl = `
{{ if not (ignored .) -}}
{{ if disabled . -}}
	{{ cmt "Validate is disabled for " (msgTyp .) ". This method will always return nil." }}
{{- else -}}
	{{ cmt "Validate checks the field values on " (msgTyp .) " with the rules defined in the proto definition for this message. If any rules are violated, the first error encountered is returned, or nil if there are no violations." }}
{{- end -}}
func (m {{ (msgTyp .).Pointer }}) Validate() error {
	return m.validate(false)
}

{{ if disabled . -}}
	{{ cmt "ValidateAll is disabled for " (msgTyp .) ". This method will always return nil." }}
{{- else -}}
	{{ cmt "ValidateAll checks the field values on " (msgTyp .) " with the rules defined in the proto definition for this message. If any rules are violated, the result is a list of violation errors wrapped in " (multierrname .) ", or nil if none found." }}
{{- end -}}
func (m {{ (msgTyp .).Pointer }}) ValidateAll() error {
	return m.validate(true)
}

{{/* Unexported function to handle validation. If the need arises to add more exported functions, please consider the functional option approach outlined in protoc-gen-validate#47. */}}
func (m {{ (msgTyp .).Pointer }}) validate(all bool) error {
	{{ if disabled . -}}
		return nil
	{{ else -}}
		if m == nil { return nil }

		var errors []error

		{{ range .NonOneOfFields }}
			{{ render (context .) }}
		{{ end }}

		{{ range .RealOneOfs }}
			{{- $oneof := . }}
			{{- if required . }}
			oneof{{ name $oneof }}Present := false
			{{- end }}
			switch v := m.{{ name . }}.(type) {
				{{- range .Fields }}
					{{- $context := (context .) }}
					case {{ oneof . }}:
						if v == nil {
							err := {{ errname .Message }}{
								field: "{{ name $oneof }}",
								reason: "oneof value cannot be a typed-nil",
							}
							if !all { return err }
							errors = append(errors, err)
						}
						{{- if required $oneof }}
						oneof{{ name $oneof }}Present = true
						{{- end }}
						{{ render $context }}
				{{- end }}
					default:
						_ = v // ensures v is used
			}
			{{- if required . }}
			if !oneof{{ name $oneof }}Present {
				err := {{ errname .Message }}{
					field: "{{ name $oneof }}",
					reason: "value is required",
				}
				if !all { return err }
				errors = append(errors, err)
			}
			{{- end }}
		{{- end }}

		{{ range .SyntheticOneOfFields }}
			if m.{{ name . }} != nil {
				{{ render (context .) }}
			}
		{{ end }}

		if len(errors) > 0 {
			return {{ multierrname . }}(errors)
		}

		return nil
	{{ end -}}
}

{{ if needs . "hostname" }}{{ template "hostname" . }}{{ end }}

{{ if needs . "email" }}{{ template "email" . }}{{ end }}

{{ if needs . "uuid" }}{{ template "uuid" . }}{{ end }}

{{ cmt (multierrname .) " is an error wrapping multiple validation errors returned by " (msgTyp .) ".ValidateAll() if the designated constraints aren't met." -}}
type {{ multierrname . }} []error

// Error returns a concatenation of all the error messages it wraps.
func (m {{ multierrname . }}) Error() string {
	msgs := make([]string, 0, len(m))
	for _, err := range m {
		msgs = append(msgs, err.Error())
	}
	return strings.Join(msgs, "; ")
}

// AllErrors returns a list of validation violation errors.
func (m {{ multierrname . }}) AllErrors() []error { return m }

{{ cmt (errname .) " is the validation error returned by " (msgTyp .) ".Validate if the designated constraints aren't met." -}}
type {{ errname . }} struct {
	field  string
	reason string
	cause  error
	key    bool
}

// Field function returns field value.
func (e {{ errname . }}) Field() string { return e.field }

// Reason function returns reason value.
func (e {{ errname . }}) Reason() string { return e.reason }

// Cause function returns cause value.
func (e {{ errname . }}) Cause() error { return e.cause }

// Key function returns key value.
func (e {{ errname . }}) Key() bool { return e.key }

// ErrorName returns error name.
func (e {{ errname . }}) ErrorName() string { return "{{ errname . }}" }

// Error satisfies the builtin error interface
func (e {{ errname . }}) Error() string {
	cause := ""
	if e.cause != nil {
		cause = fmt.Sprintf(" | caused by: %v", e.cause)
	}

	key := ""
	if e.key {
		key = "key for "
	}

	return fmt.Sprintf(
		"invalid %s{{ (msgTyp .) }}.%s: %s%s",
		key,
		e.field,
		e.reason,
		cause)
}

var _ error = {{ errname . }}{}

var _ interface{
	Field() string
	Reason() string
	Key() bool
	Cause() error
	ErrorName() string
} = {{ errname . }}{}

{{ range .Fields }}{{ with (context .) }}{{ $f := .Field }}
	{{ if has .Rules "In" }}{{ if .Rules.In }}
		var {{ lookup .Field "InLookup" }} = map[{{ inType .Field .Rules.In }}]struct{}{
			{{- range .Rules.In }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}

	{{ if has .Rules "NotIn" }}{{ if .Rules.NotIn }}
		var {{ lookup .Field "NotInLookup" }} = map[{{ inType .Field .Rules.In }}]struct{}{
			{{- range .Rules.NotIn }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}

	{{ if has .Rules "Pattern"}}{{ if .Rules.Pattern }}
		var {{ lookup .Field "Pattern" }} = regexp.MustCompile({{ lit .Rules.GetPattern }})
	{{ end }}{{ end }}

	{{ if has .Rules "Items"}}{{ if .Rules.Items }}
	{{ if has .Rules.Items.GetString_ "Pattern" }} {{ if .Rules.Items.GetString_.Pattern }}
		var {{ lookup .Field "Pattern" }} = regexp.MustCompile({{ lit .Rules.Items.GetString_.GetPattern }})
	{{ end }}{{ end }}
	{{ end }}{{ end }}

	{{ if has .Rules "Items"}}{{ if .Rules.Items }}
	{{ if has .Rules.Items.GetString_ "In" }} {{ if .Rules.Items.GetString_.In }}
		var {{ lookup .Field "InLookup" }} = map[string]struct{}{
			{{- range .Rules.Items.GetString_.In }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetEnum "In" }} {{ if .Rules.Items.GetEnum.In }}
		var {{ lookup .Field "InLookup" }} = map[{{ inType .Field .Rules.Items.GetEnum.In }}]struct{}{
			{{- range .Rules.Items.GetEnum.In }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetInt64 "In" }} {{ if .Rules.Items.GetInt64.In }}
		var {{ lookup .Field "InLookup" }} = map[int64]struct{}{
			{{- range .Rules.Items.GetInt64.In }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetInt64 "NotIn" }} {{ if .Rules.Items.GetInt64.NotIn }}
		var {{ lookup .Field "NotInLookup" }} = map[int64]struct{}{
			{{- range .Rules.Items.GetInt64.NotIn }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetInt32 "In" }} {{ if .Rules.Items.GetInt32.In }}
		var {{ lookup .Field "InLookup" }} = map[int32]struct{}{
			{{- range .Rules.Items.GetInt32.In }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetInt32 "NotIn" }} {{ if .Rules.Items.GetInt32.NotIn }}
		var {{ lookup .Field "NotInLookup" }} = map[int32]struct{}{
			{{- range .Rules.Items.GetInt32.NotIn }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetAny "In" }} {{ if .Rules.Items.GetAny.In }}
		var {{ lookup .Field "InLookup" }} = map[string]struct{}{
			{{- range .Rules.Items.GetAny.In }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ end }}{{ end }}

	{{ if has .Rules "Items"}}{{ if .Rules.Items }}
	{{ if has .Rules.Items.GetString_ "NotIn" }} {{ if .Rules.Items.GetString_.NotIn }}
		var {{ lookup .Field "NotInLookup" }} = map[string]struct{}{
			{{- range .Rules.Items.GetString_.NotIn }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetEnum "NotIn" }} {{ if .Rules.Items.GetEnum.NotIn }}
		var {{ lookup .Field "NotInLookup" }} = map[{{ inType .Field .Rules.Items.GetEnum.NotIn }}]struct{}{
			{{- range .Rules.Items.GetEnum.NotIn }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetAny "NotIn" }} {{ if .Rules.Items.GetAny.NotIn }}
		var {{ lookup .Field "NotInLookup" }} = map[string]struct{}{
			{{- range .Rules.Items.GetAny.NotIn }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ end }}{{ end }}

	{{ if has .Rules "Keys"}}{{ if .Rules.Keys }}
	{{ if has .Rules.Keys.GetString_ "In" }} {{ if .Rules.Keys.GetString_.In }}
		var {{ lookup .Field "InLookup" }} = map[{{ inType .Field .Rules.Keys.GetString_.In }}]struct{}{
			{{- range .Rules.Keys.GetString_.In }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Keys.GetString_ "NotIn" }} {{ if .Rules.Keys.GetString_.NotIn }}
		var {{ lookup .Field "NotInLookup" }} = map[{{ inType .Field .Rules.Keys.GetString_.NotIn }}]struct{}{
			{{- range .Rules.Keys.GetString_.NotIn }}
				{{ inKey $f . }}: {},
			{{- end }}
		}
	{{ end }}{{ end }}
	{{ if has .Rules.Keys.GetString_ "Pattern" }} {{ if .Rules.Keys.GetString_.Pattern }}
		var {{ lookup .Field "Pattern" }} = regexp.MustCompile({{ lit .Rules.Keys.GetString_.GetPattern }})
	{{ end }}{{ end }}
	{{ end }}{{ end }}

	{{ if has .Rules "Values"}}{{ if .Rules.Values }}
	{{ if has .Rules.Values.GetString_ "Pattern" }} {{ if .Rules.Values.GetString_.Pattern }}
		var {{ lookup .Field "Pattern" }} = regexp.MustCompile({{ lit .Rules.Values.GetString_.GetPattern }})
	{{ end }}{{ end }}
	{{ end }}{{ end }}

{{ end }}{{ end }}
{{- end -}}
`
