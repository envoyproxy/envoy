all: server

server:
	@CGO_ENABLED=0 GOOS=linux go build -a --ldflags '-extldflags "-static"' \
		-tags "netgo" -installsuffix netgo \
		-o server
clean:
	@rm -fr server
