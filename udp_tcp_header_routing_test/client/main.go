package main

import (
	"bufio"
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"strconv"
	"strings"
	"time"
)

// buildHeader 构造 8 字节协议头：[Magic 1B][Version 1B][RoomIP 4B 大端][RoomPort 2B 大端]。
// 与 Envoy header_routing 过滤器配置（envoy.yaml: magic=85, version=1）严格对应。
func buildHeader(roomIP string, roomPort uint16) ([]byte, error) {
	ip := net.ParseIP(roomIP).To4()
	if ip == nil {
		return nil, fmt.Errorf("无效的 IPv4 地址: %s", roomIP)
	}
	header := make([]byte, 8)
	header[0] = 0x55 // Magic，与 envoy.yaml 的 magic:85 一致
	header[1] = 0x01 // Version，与 envoy.yaml 的 version:1 一致
	copy(header[2:6], ip)
	binary.BigEndian.PutUint16(header[6:8], roomPort)
	return header, nil
}

// 头部添加模式：控制客户端发送的每个数据包是否都带 8B 协议头。
//   - all（默认）：所有包都带头，与 Envoy forward_header=true（头部透传）配套；
//   - first：仅首包带头，后续包不带（与 Envoy forward_header=false（剥头）配套）。
const (
	HeaderModeAll   = "all"
	HeaderModeFirst = "first"
)

// Client 客户端接口
type Client interface {
	Connect() error
	SendMessage(message string) (string, error)
	Close()
}

// UDPClient UDP客户端（头部模式：all=每包带头 / first=仅首包带头）
type UDPClient struct {
	ServerHost string
	ServerPort int
	RoomIP     string
	RoomPort   uint16
	Conn       *net.UDPConn
	headerMode string // all=所有包带头；first=仅首包带头
	headerSent bool   // 已发送过带头包（first 模式用）
}

// NewUDPClient 创建新的UDP客户端
func NewUDPClient(host string, port int, roomIP string, roomPort uint16, headerMode string) *UDPClient {
	return &UDPClient{ServerHost: host, ServerPort: port, RoomIP: roomIP, RoomPort: roomPort, headerMode: headerMode}
}

// Connect 连接到Envoy的UDP监听端口
func (c *UDPClient) Connect() error {
	udpAddr, err := net.ResolveUDPAddr("udp", net.JoinHostPort(c.ServerHost, strconv.Itoa(c.ServerPort)))
	if err != nil {
		return fmt.Errorf("解析Envoy地址失败: %v", err)
	}
	conn, err := net.DialUDP("udp", nil, udpAddr)
	if err != nil {
		return fmt.Errorf("连接Envoy失败: %v", err)
	}
	c.Conn = conn
	log.Printf("✅ 已连接到Envoy UDP代理: %s", udpAddr)
	log.Printf("🎯 目标房间服务器: %s:%d", c.RoomIP, c.RoomPort)
	return nil
}

// SendMessage 发送消息：按 headerMode 决定是否带头。
//   - all：每个数据包都前置 8B 头（验证 Envoy forward_header=true 头部透传）；
//   - first：仅首包带头，之后只发纯数据（验证"仅首包选路"契约）。
func (c *UDPClient) SendMessage(message string) (string, error) {
	payload := []byte(message)
	withHeader := c.headerMode == HeaderModeAll || !c.headerSent
	if withHeader {
		header, err := buildHeader(c.RoomIP, c.RoomPort)
		if err != nil {
			return "", err
		}
		payload = append(header, payload...)
		c.headerSent = true
	}
	if _, err := c.Conn.Write(payload); err != nil {
		return "", fmt.Errorf("发送消息失败: %v", err)
	}
	log.Printf("📤 [UDP] %s（%s）", message, map[bool]string{true: "带头", false: "不带头"}[withHeader])
	c.Conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	buffer := make([]byte, 1024)
	n, _, err := c.Conn.ReadFromUDP(buffer)
	if err != nil {
		return "", fmt.Errorf("接收响应失败: %v", err)
	}
	return string(buffer[:n]), nil
}

// Close 关闭UDP连接
func (c *UDPClient) Close() {
	if c.Conn != nil {
		c.Conn.Close()
		log.Printf("🔌 UDP连接已关闭")
	}
}

// TCPClient TCP客户端（头部模式：all=每包带头 / first=仅首包带头）
type TCPClient struct {
	ServerHost string
	ServerPort int
	RoomIP     string
	RoomPort   uint16
	Conn       net.Conn
	Reader     *bufio.Reader
	headerMode string // all=所有包带头；first=仅首包带头
	headerSent bool   // 已发送过带头包（first 模式用）
}

// NewTCPClient 创建新的TCP客户端
func NewTCPClient(host string, port int, roomIP string, roomPort uint16, headerMode string) *TCPClient {
	return &TCPClient{ServerHost: host, ServerPort: port, RoomIP: roomIP, RoomPort: roomPort, headerMode: headerMode}
}

// Connect 连接到Envoy的TCP监听端口
func (c *TCPClient) Connect() error {
	conn, err := net.Dial("tcp", net.JoinHostPort(c.ServerHost, strconv.Itoa(c.ServerPort)))
	if err != nil {
		return fmt.Errorf("连接Envoy失败: %v", err)
	}
	c.Conn = conn
	c.Reader = bufio.NewReader(conn)
	log.Printf("✅ 已连接到Envoy TCP代理: %s:%d", c.ServerHost, c.ServerPort)
	log.Printf("🎯 目标房间服务器: %s:%d", c.RoomIP, c.RoomPort)
	return nil
}

// SendMessage 发送消息：按 headerMode 决定是否带头。
//   - all：每个数据包都前置 8B 头（验证 Envoy forward_header=true 头部透传）；
//   - first：仅首包带头，之后只发纯数据。
//
// 复用 Reader 避免丢失缓冲；服务器按行回复，读一行作为响应。
func (c *TCPClient) SendMessage(message string) (string, error) {
	payload := []byte(message + "\n")
	withHeader := c.headerMode == HeaderModeAll || !c.headerSent
	if withHeader {
		header, err := buildHeader(c.RoomIP, c.RoomPort)
		if err != nil {
			return "", err
		}
		payload = append(header, payload...)
		c.headerSent = true
	}
	if _, err := c.Conn.Write(payload); err != nil {
		return "", fmt.Errorf("发送消息失败: %v", err)
	}
	log.Printf("📤 [TCP] %s（%s）", message, map[bool]string{true: "带头", false: "不带头"}[withHeader])
	c.Conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	response, err := c.Reader.ReadString('\n')
	if err != nil {
		return "", fmt.Errorf("接收响应失败: %v", err)
	}
	return strings.TrimSpace(response), nil
}

// Close 关闭TCP连接
func (c *TCPClient) Close() {
	if c.Conn != nil {
		c.Conn.Close()
		log.Printf("🔌 TCP连接已关闭")
	}
}

// envOr 读取环境变量，为空时返回默认值
func envOr(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func main() {
	// 命令行参数（均可用环境变量兜底，便于容器化）
	host := flag.String("host", envOr("SERVER_HOST", "localhost"), "Envoy监听地址")
	udpPort := flag.Int("udp-port", 10000, "Envoy UDP监听端口")
	tcpPort := flag.Int("tcp-port", 10001, "Envoy TCP监听端口")
	protocol := flag.String("protocol", envOr("PROTOCOL", "udp"), "协议模式: udp/tcp/both")
	roomIP := flag.String("room-ip", envOr("ROOM_IP", ""), "目标房间服务器IP")
	roomPort := flag.Uint("room-port", 0, "目标房间服务器端口")
	headerMode := flag.String("header-mode", envOr("HEADER_MODE", HeaderModeAll), "头部模式: all=所有包带头 / first=仅首包带头")
	ping := flag.Bool("ping", false, "是否发送 PING 消息")
	flag.Parse()

	if *roomIP == "" || *roomPort == 0 {
		log.Fatal("❌ 必须指定 -room-ip 和 -room-port（目标房间服务器地址）")
	}

	proto := strings.ToLower(*protocol)
	if proto != "udp" && proto != "tcp" && proto != "both" {
		log.Fatalf("❌ 无效的协议模式: %s（支持 udp/tcp/both）", *protocol)
	}
	hm := strings.ToLower(*headerMode)
	if hm != HeaderModeAll && hm != HeaderModeFirst {
		log.Fatalf("❌ 无效的头部模式: %s（支持 %s=%s 所有包带头 / %s=仅首包带头）", *headerMode, HeaderModeAll, HeaderModeAll, HeaderModeFirst)
	}

	log.Printf("🚀 Envoy HeaderRouting 测试客户端")
	log.Printf("================================")
	log.Printf("📡 Envoy地址: %s (UDP:%d / TCP:%d)", *host, *udpPort, *tcpPort)
	log.Printf("🎯 房间服务器: %s:%d", *roomIP, *roomPort)
	log.Printf("🧭 头部模式: %s（%s）", hm, map[string]string{HeaderModeAll: "所有包带头", HeaderModeFirst: "仅首包带头"}[hm])

	// 按协议模式创建并连接客户端
	var clients []Client
	roomPort16 := uint16(*roomPort)
	if proto == "udp" || proto == "both" {
		clients = append(clients, NewUDPClient(*host, *udpPort, *roomIP, roomPort16, hm))
	}
	if proto == "tcp" || proto == "both" {
		clients = append(clients, NewTCPClient(*host, *tcpPort, *roomIP, roomPort16, hm))
	}
	for _, client := range clients {
		if err := client.Connect(); err != nil {
			log.Fatalf("❌ 连接失败: %v", err)
		}
	}
	defer func() {
		for _, client := range clients {
			client.Close()
		}
	}()

	// 自动发送一轮测试消息：
	// PING（建会话）/ BATTLE（战斗消息）/ STATUS（状态消息）/ PING（验证确认后不带头）
	// -ping 模式下仅发送 PING，用于单独验证建会话与"确认后不带头"契约
	testMessages := []string{
		"PING",
		"BATTLE attack enemy-123",
		"STATUS",
		"PING",
	}
	if *ping {
		testMessages = []string{"PING", "PING"}
	}
	for _, msg := range testMessages {
		for i, client := range clients {
			protoName := "UDP"
			if i == 1 {
				protoName = "TCP"
			}
			response, err := client.SendMessage(msg)
			if err != nil {
				log.Printf("❌ [%s] %s 发送失败: %v", protoName, msg, err)
				continue
			}
			log.Printf("📨 [%s] 服务器响应: %s", protoName, response)
		}
	}
	log.Printf("✅ 测试完成")
}
