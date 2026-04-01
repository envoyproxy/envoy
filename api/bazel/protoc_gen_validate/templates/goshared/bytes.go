package goshared

const bytesTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}

	{{ if $r.GetIgnoreEmpty }}
		if len({{ accessor . }}) > 0 {
	{{ end }}

	{{ if or $r.Len (and $r.MinLen $r.MaxLen (eq $r.GetMinLen $r.GetMaxLen)) }}
		{{ if $r.Len }}
			if len({{ accessor . }}) != {{ $r.GetLen }} {
				err := {{ err . "value length must be " $r.GetLen " bytes" }}
				if !all { return err }
				errors = append(errors, err)
			}
		{{ else }}
			if len({{ accessor . }}) != {{ $r.GetMinLen }} {
				err := {{ err . "value length must be " $r.GetMinLen " bytes" }}
				if !all { return err }
				errors = append(errors, err)
			}
		{{ end }}
	{{ else if $r.MinLen }}
		{{ if $r.MaxLen }}
			if l := len({{ accessor . }}); l < {{ $r.GetMinLen }} || l > {{ $r.GetMaxLen }} {
				err := {{ err . "value length must be between " $r.GetMinLen " and " $r.GetMaxLen " bytes, inclusive" }}
				if !all { return err }
				errors = append(errors, err)
			}
		{{ else }}
			if len({{ accessor . }}) < {{ $r.GetMinLen }} {
				err := {{ err . "value length must be at least " $r.GetMinLen " bytes" }}
				if !all { return err }
				errors = append(errors, err)
			}
		{{ end }}
	{{ else if $r.MaxLen }}
		if len({{ accessor . }}) > {{ $r.GetMaxLen }} {
			err := {{ err . "value length must be at most " $r.GetMaxLen " bytes" }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}

	{{ if $r.Prefix }}
		if !bytes.HasPrefix({{ accessor . }}, {{ lit $r.GetPrefix }}) {
			err := {{ err . "value does not have prefix " (byteStr $r.GetPrefix) }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}

	{{ if $r.Suffix }}
		if !bytes.HasSuffix({{ accessor . }}, {{ lit $r.GetSuffix }}) {
			err := {{ err . "value does not have suffix " (byteStr $r.GetSuffix) }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}

	{{ if $r.Contains }}
		if !bytes.Contains({{ accessor . }}, {{ lit $r.GetContains }}) {
			err := {{ err . "value does not contain " (byteStr $r.GetContains) }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}

	{{ if $r.In }}
		if _, ok := {{ lookup $f "InLookup" }}[string({{ accessor . }})]; !ok {
			err := {{ err . "value must be in list " $r.In }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ else if $r.NotIn }}
		if _, ok := {{ lookup $f "NotInLookup" }}[string({{ accessor . }})]; ok {
			err := {{ err . "value must not be in list " $r.NotIn }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}

	{{ if $r.Const }}
		if !bytes.Equal({{ accessor . }}, {{ lit $r.Const }}) {
			err := {{ err . "value must equal " $r.Const }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}

	{{ if $r.GetIp }}
		if ip := net.IP({{ accessor . }}); ip.To16() == nil {
			err := {{ err . "value must be a valid IP address" }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ else if $r.GetIpv4 }}
		if ip := net.IP({{ accessor . }}); ip.To4() == nil {
			err := {{ err . "value must be a valid IPv4 address" }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ else if $r.GetIpv6 }}
		if ip := net.IP({{ accessor . }}); ip.To16() == nil || ip.To4() != nil {
			err := {{ err . "value must be a valid IPv6 address" }}
			if !all { return err }
			errors = append(errors, err)
		}
	{{ end }}

	{{ if $r.Pattern }}
	if !{{ lookup $f "Pattern" }}.Match({{ accessor . }}) {
		err := {{ err . "value does not match regex pattern " (lit $r.GetPattern) }}
		if !all { return err }
		errors = append(errors, err)
	}
	{{ end }}

	{{ if $r.GetIgnoreEmpty }}
		}
	{{ end }}
`
