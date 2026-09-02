FROM golang:1.21-alpine

WORKDIR /app

# 复制go mod文件
COPY go.mod ./

# 下载依赖
RUN go mod download

# 复制源代码
COPY . .

# 构建应用
RUN go build -o test-client .

# 运行应用
CMD ["./test-client"]