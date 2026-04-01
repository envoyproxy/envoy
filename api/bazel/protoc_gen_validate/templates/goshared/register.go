package goshared

import (
	"fmt"
	"reflect"
	"strconv"
	"strings"
	"text/template"

	"github.com/iancoleman/strcase"
	pgs "github.com/lyft/protoc-gen-star/v2"
	pgsgo "github.com/lyft/protoc-gen-star/v2/lang/go"
	"google.golang.org/protobuf/types/known/durationpb"
	"google.golang.org/protobuf/types/known/timestamppb"

	"github.com/envoyproxy/protoc-gen-validate/templates/shared"
)

func Register(tpl *template.Template, params pgs.Parameters) {
	fns := goSharedFuncs{pgsgo.InitContext(params)}

	tpl.Funcs(map[string]interface{}{
		"accessor":      fns.accessor,
		"byteStr":       fns.byteStr,
		"cmt":           pgs.C80,
		"durGt":         fns.durGt,
		"durLit":        fns.durLit,
		"durStr":        fns.durStr,
		"err":           fns.err,
		"errCause":      fns.errCause,
		"errIdx":        fns.errIdx,
		"errIdxCause":   fns.errIdxCause,
		"errname":       fns.errName,
		"multierrname":  fns.multiErrName,
		"inKey":         fns.inKey,
		"inType":        fns.inType,
		"isBytes":       fns.isBytes,
		"lit":           fns.lit,
		"lookup":        fns.lookup,
		"msgTyp":        fns.msgTyp,
		"name":          fns.Name,
		"oneof":         fns.oneofTypeName,
		"pkg":           fns.PackageName,
		"snakeCase":     fns.snakeCase,
		"tsGt":          fns.tsGt,
		"tsLit":         fns.tsLit,
		"tsStr":         fns.tsStr,
		"typ":           fns.Type,
		"unwrap":        fns.unwrap,
		"externalEnums": fns.externalEnums,
		"enumName":      fns.enumName,
		"enumPackages":  fns.enumPackages,
	})

	template.Must(tpl.New("msg").Parse(msgTpl))
	template.Must(tpl.New("const").Parse(constTpl))
	template.Must(tpl.New("ltgt").Parse(ltgtTpl))
	template.Must(tpl.New("in").Parse(inTpl))

	template.Must(tpl.New("none").Parse(noneTpl))
	template.Must(tpl.New("float").Parse(numTpl))
	template.Must(tpl.New("double").Parse(numTpl))
	template.Must(tpl.New("int32").Parse(numTpl))
	template.Must(tpl.New("int64").Parse(numTpl))
	template.Must(tpl.New("uint32").Parse(numTpl))
	template.Must(tpl.New("uint64").Parse(numTpl))
	template.Must(tpl.New("sint32").Parse(numTpl))
	template.Must(tpl.New("sint64").Parse(numTpl))
	template.Must(tpl.New("fixed32").Parse(numTpl))
	template.Must(tpl.New("fixed64").Parse(numTpl))
	template.Must(tpl.New("sfixed32").Parse(numTpl))
	template.Must(tpl.New("sfixed64").Parse(numTpl))

	template.Must(tpl.New("bool").Parse(constTpl))
	template.Must(tpl.New("string").Parse(strTpl))
	template.Must(tpl.New("bytes").Parse(bytesTpl))

	template.Must(tpl.New("email").Parse(emailTpl))
	template.Must(tpl.New("hostname").Parse(hostTpl))
	template.Must(tpl.New("address").Parse(hostTpl))
	template.Must(tpl.New("uuid").Parse(uuidTpl))

	template.Must(tpl.New("enum").Parse(enumTpl))
	template.Must(tpl.New("repeated").Parse(repTpl))
	template.Must(tpl.New("map").Parse(mapTpl))

	template.Must(tpl.New("any").Parse(anyTpl))
	template.Must(tpl.New("timestampcmp").Parse(timestampcmpTpl))
	template.Must(tpl.New("durationcmp").Parse(durationcmpTpl))

	template.Must(tpl.New("wrapper").Parse(wrapperTpl))
}

type goSharedFuncs struct{ pgsgo.Context }

func (fns goSharedFuncs) accessor(ctx shared.RuleContext) string {
	if ctx.AccessorOverride != "" {
		return ctx.AccessorOverride
	}

	return fmt.Sprintf("m.Get%s()", fns.Name(ctx.Field))
}

func (fns goSharedFuncs) errName(m pgs.Message) pgs.Name {
	return fns.Name(m) + "ValidationError"
}

func (fns goSharedFuncs) multiErrName(m pgs.Message) pgs.Name {
	return fns.Name(m) + "MultiError"
}

func (fns goSharedFuncs) errIdxCause(ctx shared.RuleContext, idx, cause string, reason ...interface{}) string {
	f := ctx.Field
	n := fns.Name(f)

	var fld string
	switch {
	case idx != "":
		fld = fmt.Sprintf(`fmt.Sprintf("%s[%%v]", %s)`, n, idx)
	case ctx.Index != "":
		fld = fmt.Sprintf(`fmt.Sprintf("%s[%%v]", %s)`, n, ctx.Index)
	default:
		fld = fmt.Sprintf("%q", n)
	}

	causeFld := ""
	if cause != "nil" && cause != "" {
		causeFld = fmt.Sprintf("cause: %s,", cause)
	}

	keyFld := ""
	if ctx.OnKey {
		keyFld = "key: true,"
	}

	return fmt.Sprintf(`%s{
		field: %s,
		reason: %q,
		%s%s
	}`,
		fns.errName(f.Message()),
		fld,
		fmt.Sprint(reason...),
		causeFld,
		keyFld)
}

func (fns goSharedFuncs) err(ctx shared.RuleContext, reason ...interface{}) string {
	return fns.errIdxCause(ctx, "", "nil", reason...)
}

func (fns goSharedFuncs) errCause(ctx shared.RuleContext, cause string, reason ...interface{}) string {
	return fns.errIdxCause(ctx, "", cause, reason...)
}

func (fns goSharedFuncs) errIdx(ctx shared.RuleContext, idx string, reason ...interface{}) string {
	return fns.errIdxCause(ctx, idx, "nil", reason...)
}

func (fns goSharedFuncs) lookup(f pgs.Field, name string) string {
	return fmt.Sprintf(
		"_%s_%s_%s",
		fns.Name(f.Message()),
		fns.Name(f),
		name,
	)
}

func (fns goSharedFuncs) lit(x interface{}) string {
	val := reflect.ValueOf(x)

	if val.Kind() == reflect.Interface {
		val = val.Elem()
	}

	if val.Kind() == reflect.Ptr {
		val = val.Elem()
	}

	switch val.Kind() {
	case reflect.String:
		return fmt.Sprintf("%q", x)
	case reflect.Uint8:
		return fmt.Sprintf("0x%X", x)
	case reflect.Slice:
		els := make([]string, val.Len())
		for i, l := 0, val.Len(); i < l; i++ {
			els[i] = fns.lit(val.Index(i).Interface())
		}
		return fmt.Sprintf("%T{%s}", val.Interface(), strings.Join(els, ", "))
	default:
		return fmt.Sprint(x)
	}
}

func (fns goSharedFuncs) isBytes(f interface {
	ProtoType() pgs.ProtoType
},
) bool {
	return f.ProtoType() == pgs.BytesT
}

func (fns goSharedFuncs) byteStr(x []byte) string {
	elms := make([]string, len(x))
	for i, b := range x {
		elms[i] = fmt.Sprintf(`\x%X`, b)
	}

	return fmt.Sprintf(`"%s"`, strings.Join(elms, ""))
}

func (fns goSharedFuncs) oneofTypeName(f pgs.Field) pgsgo.TypeName {
	return pgsgo.TypeName(fns.OneofOption(f)).Pointer()
}

func (fns goSharedFuncs) inType(f pgs.Field, x interface{}) string {
	switch f.Type().ProtoType() {
	case pgs.BytesT:
		return "string"
	case pgs.MessageT:
		switch x.(type) {
		case []*durationpb.Duration:
			return "time.Duration"
		default:
			return pgsgo.TypeName(fmt.Sprintf("%T", x)).Element().String()
		}
	case pgs.EnumT:
		ens := fns.enumPackages(fns.externalEnums(f.File()))
		// Check if the imported name of the enum has collided and been renamed
		if len(ens) != 0 {
			enType := f.Type().Enum()
			if f.Type().IsRepeated() {
				enType = f.Type().Element().Enum()
			}

			enImportPath := fns.ImportPath(enType)
			for pkg, en := range ens {
				if en.FilePath == enImportPath {
					return pkg.String() + "." + fns.enumName(enType)
				}
			}
		}

		if f.Type().IsRepeated() {
			return strings.TrimLeft(fns.Type(f).String(), "[]")
		} else {
			// Use Value() to strip any potential pointer type.
			return fns.Type(f).Value().String()
		}
	default:
		// Use Value() to strip any potential pointer type.
		return fns.Type(f).Value().String()
	}
}

func (fns goSharedFuncs) inKey(f pgs.Field, x interface{}) string {
	switch f.Type().ProtoType() {
	case pgs.BytesT:
		return fns.byteStr(x.([]byte))
	case pgs.MessageT:
		switch x := x.(type) {
		case *durationpb.Duration:
			dur := x.AsDuration()
			return fns.lit(int64(dur))
		default:
			return fns.lit(x)
		}
	default:
		return fns.lit(x)
	}
}

func (fns goSharedFuncs) durLit(dur *durationpb.Duration) string {
	return fmt.Sprintf(
		"time.Duration(%d * time.Second + %d * time.Nanosecond)",
		dur.GetSeconds(), dur.GetNanos())
}

func (fns goSharedFuncs) durStr(dur *durationpb.Duration) string {
	d := dur.AsDuration()
	return d.String()
}

func (fns goSharedFuncs) durGt(a, b *durationpb.Duration) bool {
	ad := a.AsDuration()
	bd := b.AsDuration()

	return ad > bd
}

func (fns goSharedFuncs) tsLit(ts *timestamppb.Timestamp) string {
	return fmt.Sprintf(
		"time.Unix(%d, %d)",
		ts.GetSeconds(), ts.GetNanos(),
	)
}

func (fns goSharedFuncs) tsGt(a, b *timestamppb.Timestamp) bool {
	at := a.AsTime()
	bt := b.AsTime()

	return bt.Before(at)
}

func (fns goSharedFuncs) tsStr(ts *timestamppb.Timestamp) string {
	t := ts.AsTime()
	return t.String()
}

func (fns goSharedFuncs) unwrap(ctx shared.RuleContext, name string) (shared.RuleContext, error) {
	ctx, err := ctx.Unwrap("wrapper")
	if err != nil {
		return ctx, err
	}

	ctx.AccessorOverride = fmt.Sprintf("%s.Get%s()", name,
		pgsgo.PGGUpperCamelCase(ctx.Field.Type().Embed().Fields()[0].Name()))

	return ctx, nil
}

func (fns goSharedFuncs) msgTyp(message pgs.Message) pgsgo.TypeName {
	return pgsgo.TypeName(fns.Name(message))
}

func (fns goSharedFuncs) externalEnums(file pgs.File) []pgs.Enum {
	var out []pgs.Enum

	for _, msg := range file.AllMessages() {
		for _, fld := range msg.Fields() {
			var en pgs.Enum

			if fld.Type().IsEnum() {
				en = fld.Type().Enum()
			}

			if fld.Type().IsRepeated() {
				en = fld.Type().Element().Enum()
			}

			if en != nil && en.File().Package().ProtoName() != msg.File().Package().ProtoName() {
				out = append(out, en)
			}
		}
	}

	return out
}

func (fns goSharedFuncs) enumName(enum pgs.Enum) string {
	out := string(enum.Name())
	parent := enum.Parent()
	for {
		message, ok := parent.(pgs.Message)
		if ok {
			out = string(message.Name()) + "_" + out
			parent = message.Parent()
		} else {
			return out
		}
	}
}

type NormalizedEnum struct {
	FilePath pgs.FilePath
	Name     string
}

func (fns goSharedFuncs) enumPackages(enums []pgs.Enum) map[pgs.Name]NormalizedEnum {
	out := make(map[pgs.Name]NormalizedEnum, len(enums))

	// Start point from ./templates/go/file.go
	nameCollision := map[pgs.Name]int{
		"bytes":   0,
		"errors":  0,
		"fmt":     0,
		"net":     0,
		"mail":    0,
		"url":     0,
		"regexp":  0,
		"sort":    0,
		"strings": 0,
		"time":    0,
		"utf8":    0,
		"anypb":   0,
	}
	nameNormalized := make(map[pgs.FilePath]struct{})

	for _, en := range enums {
		enImportPath := fns.ImportPath(en)
		if _, ok := nameNormalized[enImportPath]; ok {
			continue
		}

		pkgName := fns.PackageName(en)

		if collision, ok := nameCollision[pkgName]; ok {
			nameCollision[pkgName] = collision + 1
			pkgName += pgs.Name(strconv.Itoa(nameCollision[pkgName]))
		} else {
			nameCollision[pkgName] = 0
		}

		nameNormalized[enImportPath] = struct{}{}
		out[pkgName] = NormalizedEnum{
			Name:     fns.enumName(en),
			FilePath: enImportPath,
		}

	}

	return out
}

func (fns goSharedFuncs) snakeCase(name string) string {
	return strcase.ToSnake(name)
}
