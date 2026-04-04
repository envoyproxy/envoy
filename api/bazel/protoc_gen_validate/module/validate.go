package module

import (
	"path/filepath"
	"strings"

	pgs "github.com/lyft/protoc-gen-star/v2"
	pgsgo "github.com/lyft/protoc-gen-star/v2/lang/go"

	"github.com/envoyproxy/protoc-gen-validate/templates"
	"github.com/envoyproxy/protoc-gen-validate/templates/java"
)

const (
	validatorName = "validator"
	langParam     = "lang"
	moduleParam   = "module"
)

type Module struct {
	*pgs.ModuleBase
	ctx pgsgo.Context
	// lang contains the selected language (one of 'cc', 'go', 'java').
	// It is initialized in ValidatorForLanguage.
	// If unset, it will be parsed as the 'lang' parameter.
	lang string
}

func Validator() pgs.Module { return &Module{ModuleBase: &pgs.ModuleBase{}} }

func ValidatorForLanguage(lang string) pgs.Module {
	return &Module{lang: lang, ModuleBase: &pgs.ModuleBase{}}
}

func (m *Module) InitContext(ctx pgs.BuildContext) {
	m.ModuleBase.InitContext(ctx)
	m.ctx = pgsgo.InitContext(ctx.Parameters())
}

func (m *Module) Name() string { return validatorName }

func (m *Module) Execute(targets map[string]pgs.File, pkgs map[string]pgs.Package) []pgs.Artifact {
	lang := m.lang
	langParamValue := m.Parameters().Str(langParam)
	if lang == "" {
		lang = langParamValue
		m.Assert(lang != "", "`lang` parameter must be set")
	} else if langParamValue != "" {
		m.Fail("unknown `lang` parameter")
	}

	module := m.Parameters().Str(moduleParam)

	// Process file-level templates
	tpls := templates.Template(m.Parameters())[lang]
	m.Assert(tpls != nil, "could not find templates for `lang`: ", lang)

	for _, f := range targets {
		m.Push(f.Name().String())

		for _, msg := range f.AllMessages() {
			m.CheckRules(msg)
		}

		for _, tpl := range tpls {
			out := templates.FilePathFor(tpl)(f, m.ctx, tpl)

			// A nil path means no output should be generated for this file - as controlled by
			// implementation-specific FilePathFor implementations.
			// Ex: Don't generate Java validators for files that don't reference PGV.
			if out != nil {
				outPath := strings.TrimLeft(strings.ReplaceAll(filepath.ToSlash(out.String()), module, ""), "/")

				if opts := f.Descriptor().GetOptions(); opts != nil && opts.GetJavaMultipleFiles() && lang == "java" {
					// TODO: Only Java supports multiple file generation. If more languages add multiple file generation
					// support, the implementation should be made more inderect.
					for _, msg := range f.Messages() {
						m.AddGeneratorTemplateFile(java.JavaMultiFilePath(f, msg).String(), tpl, msg)
					}
				} else {
					m.AddGeneratorTemplateFile(outPath, tpl, f)
				}
			}
		}

		m.Pop()
	}

	return m.Artifacts()
}

var _ pgs.Module = (*Module)(nil)
