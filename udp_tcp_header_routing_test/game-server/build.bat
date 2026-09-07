
set GOOS=linux
set GOARCH=amd64
set CGO_ENABLED=0
set GO111MODULE=on

set VERSION=1
go build -ldflags="-s -w -X 'main.VERSION=0.0.%VERSION%'" -o game-server