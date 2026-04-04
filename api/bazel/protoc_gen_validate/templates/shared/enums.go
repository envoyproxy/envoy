package shared

import (
	"fmt"
	"strings"

	pgs "github.com/lyft/protoc-gen-star/v2"
)

func isEnum(f pgs.Field) bool {
	return f.Type().IsEnum()
}

func enumNamesMap(values []pgs.EnumValue) (m map[int32]string) {
	m = make(map[int32]string)
	for _, v := range values {
		if _, exists := m[v.Value()]; !exists {
			m[v.Value()] = v.Name().String()
		}
	}
	return m
}

// enumList - if type is ENUM, enum values are returned
func enumList(f pgs.Field, list []int32) string {
	stringList := make([]string, 0, len(list))
	if enum := f.Type().Enum(); enum != nil {
		names := enumNamesMap(enum.Values())
		for _, n := range list {
			stringList = append(stringList, names[n])
		}
	} else {
		for _, n := range list {
			stringList = append(stringList, fmt.Sprint(n))
		}
	}
	return "[" + strings.Join(stringList, " ") + "]"
}

// enumVal - if type is ENUM, enum value is returned
func enumVal(f pgs.Field, val int32) string {
	if enum := f.Type().Enum(); enum != nil {
		return enumNamesMap(enum.Values())[val]
	}
	return fmt.Sprint(val)
}
