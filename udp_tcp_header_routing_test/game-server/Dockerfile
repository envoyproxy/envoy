FROM golang:1.25.5-alpine

WORKDIR /app

# 复制go mod文件
COPY go.mod ./

RUN go env -w GOPROXY=https://goproxy.cn,direct
# 下载依赖
RUN go mod download

# 复制源代码
COPY . .

# 构建应用
RUN go build -o game-server .

# 暴露UDP端口和HTTP健康检查端口
EXPOSE 8080/udp
EXPOSE 9080
EXPOSE 9081
EXPOSE 9082

# 运行应用
CMD ["./game-server"]