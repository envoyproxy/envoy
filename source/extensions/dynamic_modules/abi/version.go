package abi

import (
	"crypto/sha256"
	_ "embed"
	"encoding/hex"
)

//go:embed abi.h
var abiHeaderContent []byte

var AbiHeaderVersion string

func init() {
	// Calculate ABI header version based on the content sha256 hash
	hash := sha256.New()
	_, err := hash.Write(abiHeaderContent)
	if err != nil {
		panic(err)
	}
	hashBytes := hash.Sum(nil)
	AbiHeaderVersion = hex.EncodeToString(hashBytes)
	// Append \0 to make it a C string
	AbiHeaderVersion += "\x00"
}
