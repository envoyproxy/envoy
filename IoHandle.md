
### Refactoring the FD


##### Overview

The issue with refactoring the fd is the implicit assumption that the main interface to Envoy from the application is a socket. In this PR, our purpose is to abstract away sockets and all system calls pertaining to sockets from the I/O interface. The IoHandle represents the abstract interface for all I/O operations and the details of any particular interface (socket, VPP, etc) are contained in classes derived from IoHandle. 
To this end, we also define the IoAddress class and some derivatives, as well as the IoDescriptor class, which will replace the fd() calls when dealing with actual sockets.

These changes in this PR are a proposal and are intended to get feedback for the direction we are taking. Rather than show all the changes required to implement this new direction, we will also discuss towards the end how we anticipate further changes rippling through the code.

Note also that this PR does not address events yet, as previous comments suggested. The intention is to introduce further PRs that will address events and the dispatcher.



###### IoHandle and derivative classes

The following definitions would be contained in a new file io_defs.h  (envoy/include/envoy/network):

```

// Generalize the SysCallResult to accommodate I/O operations that are not
// carried out by the kernel stack, hence not accomplished through a syscall

template <typename T> struct IoCallResult : public struct IoResult  { 
      T rc_;
       int errno_;
};
typedef IoCallResult<int> IoCallIntResult;
typedef IoCallResult<ssize_t> IoCallSizeResult;
typedef IoCallResult<void*> IoCallPtrResult;

template <typename T> class IoDescriptor  {
public:
   IoDescriptor(T descriptor):descriptor_(descriptor) { }
   virtual ~IoDescriptor() { };

   T descriptor() const { return(descriptor_); }

   operator(T) const { return(descriptor_);
   bool	operator == (const T& lhs) { return(descriptor_== lhs); }
   bool	operator != (const T& lhs) { return(descriptor_!=lhs); }

protected:
   T descriptor_;
};

typedef IoDescriptor<int> IoSocketDescriptor;  // fd for sockets
typedef IoDescriptor<uint32_t> IoVppDescriptor;  // for example…

// I/O options

struct IoOptionName { };
struct IoOptionValue { };

struct IoOptionNameSimple : public IoOptionName  { 
     std::string name_;
 };
struct IoOptionValueSimple  : public IoOptionValue { 
     std::string  value_;
 };

 struct IoSocketOptionName : public IoOptionName  { 
    Int  level_;  
    int  name_;
 };

 struct IoSocketOptionValue  : public IoOptionValue { 
    int value_;
 };

class IoHandle { 
public:
   IoHandle() { } 
   virtual ~IoHandle() { }

   // I/O Options API
   virtual IoCallResult setOption(IoOptionName name, IoOptionValue value) PURE;
   virtual const IoOptionValue& option(IoOptionName name) PURE;
   virtual size_t  options(vector<std::pair<IoOptionName,IoOptionValue>>& options) PURE;
   virtual IoCallResult applyOptions() PURE;

   virtual void setTransport(IoTransportPtr transport) PURE; 
   virtual IoTransportPtr transport() PURE;

   // Calls made from Envoy::Network::Address::Instance
   virtual IoCallResult bind() PURE;
   virtual IoCallResult connect() PURE; 
   virtual IoCallResult close() PURE;

   // Read/Write operations
   virtual IoCallResult read(Buffer::Instance& buffer) PURE;
   virtual IoCallResult write(Buffer::Instance& buffer, bool end_stream) PURE;

   // Descriptor
   const IoDescriptor& descriptor() const PURE;
};

class IoSocketHandle : public IoHandle {
public:
   IoSocketHandle(const IoDescriptor& descriptor) : descriptor_(descriptor) { }
   IoSocketHandle(const IoDescriptor& descriptor, const IoAddress& local_address)
     :descriptor_(descriptor), 
      local_addr_(local_addr){ }
	
   IoSocketHandle(const IoDescriptor& descriptor, const IoAddress& local_addr, 
        const IoAddress& remote_addr)  : descriptor_(descriptor),
        local_addr_(local_addr),  remote_addr_(remote_addr) { }
   virtual IoSocketHandle()  { }
   virtual ~IoSocketHandle() { }

   // I/O Options API
   virtual IoCallResult setOption(IoOptionName name, IoOptionValue value)  override ;
   virtual const IoOptionValue& option(IoOptionName name)  override ;
   virtual size_t  options(vector<std::pair<IoOptionName,IoOptionValue>>& options)  override ;
   virtual IoCallResult applyOptions()  override ;
   virtual void setTransport(IoTransportPtr transport) override {
             transport_socket_ = transport);
   virtual IoTransportPtr transport() override { return transport_socket_; }

   // Operations in Envoy::Network::Address::Instance
   virtual IoCallResult  bind()  override ;
   virtual IoCallResult connect()  override ;
   virtual IoCallResult close() override;
   virtual IoCallResult socket() PURE;

   // Read/Write operations
   virtual IoCallResult read(Buffer::Instance& buffer) override ;
   virtual IoCallResult write(Buffer::Instance& buffer, bool end_stream) override ;

   const IoDescriptor& descriptor() const  override { return(descriptor_); }

   // Addresses (see include/network/address.h below)
   const IoAdress&	localAddress() const { return(local_addr_); }
   const IoAdress&	remoteAddress() const { return(remote_addr_); }
   bool	 hasRemoteAddr() const { return !remote_addr_.isEmpty(); }

protected:
   IoSocketDescriptor descriptor_;
   IoAddress local_addr_;
   IoAddress	 remote_addr_;
   IoTransportSocket transport_socket_;
};

```

Similarly, we would define a new class IoHandleVpp that would accommodate differences required to interface to VPP. And it would have a customized IoTransportVpp member.



###### IoAddress and derivative classes

For the address classes, the intention was to remove all the methods using the fd. The IoAddress class is intended to replace the Instance class defined in address.h.

// in envoy/include/network/address.h:

```

enum class IoType { Stream, Datagram }; 
enum class IpVersion { v4, v6 };
enum class IoAddressType { Ipv4, Ipv6, Pipe };

class IoAddress  // equivalent to class Instance in address,h
{
public:
    virtual ~IoAddress() {}

    virtual bool operator==(const IoAddress& rhs) const PURE;
    bool operator!=(const IoAddress& rhs) const { return !operator==(rhs); }

    virtual const std::string& asString() const PURE;
    virtual const std::string& logicalName() const PURE;
      
    virtual IoAddressType type() const PURE;
    virtual bool isEmpty() const PURE;
}
typedef std::shared_ptr<const IoAddress> IoAddressConstSharedPtr;


// Replaces InstanceBase, InstanceIpv4, InstanceIpv6, IpHelper

template <typename T, typename U > class IoAddressBase : public IoAddress {
public:
    IoAddressBase() { memset(&address_, 0, sizeof(address_)); }
    IoAddressBase(T address) : address_(address) { }
    IoAddressBase(const std::string& address) : friendly_address_(address) 
        { memset(&address_, 0, sizeof(address_)); }
    virtual ~IoAddressBase() { };

    virtual const T& address() const { return address_; }
    virtual U addressBytes() const PURE;
    virtual uint32_t port() const PURE;

    virtual const std::string& asString() const override { return friendly_name_; }
    virtual const std::string& logicalName() const override { return asString(); }
    virtual T addressFromString(const std::string& address, uint32_t port=0) PURE;
    virtual const std::string& addressToString() PURE;
    virtual const std::string& addressAsString() const PURE;

    virtual bool isAnyAddress() const PURE;
    virtual bool isUnicastAddress() const PURE;

    virtual IpVersion version() const PURE;

    operator(T) const { return(address_); }
    operator(std::string) const { return(asString()); }

   bool	operator == (const T& lhs) {
      return (address_.addressBytes() == lhs.addressBytes()); } 
  bool    operator != (const T& lhs) {
      return address_.addressBytes() !=  lhs.addressBytes()); }
 
protected:

    T    		 	          address_;
    std::string    	 	friendly_address_;
    std::string    	 	friendly_name_;
    const IoAddressType 	type_;
};

// Implementations for IoAddressType. The details for Ipv4 sockets are given
// below, and the v6 socket address implementation is similar. The IoAddressPipe
// requires more thought still and is in progress.

class IoSockAddrIpv4 : public IoAddressBase<sockaddr_in, uint32_t> {
public:
   explicit IoSockAddrIpv4(const sockaddr_in& address) 
: public IoAddressBase(address),type_(Ipv4) { addressToString(); }
   explicit IoSockAddrIpv4(const sockaddr_in* address) 
: public IoAddressBase(*address),type_(Ipv4) { addressToString(); }
   explicit IoSockAddrIpv4(const std::string& address) 
: public IoAddressBase(address),type_(Ipv4) 
     { address_= addressFromString(address); }
  IoSockAddrIpv4(const std::string& address, uint32_t port) 
: public IoAddressBase(address),type_(Ipv4) 
{ address_= addressFromString(address,port); }
   explicit IoSockAddrIpv4(uint32_t port):type_(Ipv4)
{ address_= addressFromString("",port); }

   sockaddr_in address() const { return address_; }
   uint32_t addressBytes() const { return address_.sin_addr.s_addr; }
   uint32_t port() const override { return ntohs(address_.sin_port); }
   bool isEmpty() const override { return addressBytes()==0 ; }
   IoAddressType type() const override { return type_; }

   // The following is unclear- this next method replaces socketFromSocketType() but
   // maybe it should be somewhere else?
   int socketFromIoType(IoType type) const;
 
   const sockaddr_in&  addressFromString(const std::string& address,
       uint32_t port=0) override;
   const std::string& addressToString() override;
   const std::string& addressAsString() const override { return friendly_address_;
};

```

Note also that the methods addressFromFd() and peerAddressFromFd() should be methods in a derived class from a base class that has virtual methods addressFromIoHandle() and peerAddressFromIoHandle(), as opposed to addressFromSockAddr() which pertains only to sockets. 

###### Transport Sockets

The transport sockets will be redefined by the class IoTransport, with derivatives for sockets and for VPP.

In envoy/include/envoy/network, replacing the file transport_socket.h with the new file io_transport.h:


```

class IoTransportCallbacks {
public:
  virtual ~IoTransportCallbacks() {}

  virtual IoDescriptor descriptor() const PURE;
  virtual bool shouldDrainReadBuffer() PURE;
  virtual void setReadBufferReady() PURE;

  virtual Network::Connection& connection() PURE;
  virtual void raiseEvent(ConnectionEvent event) PURE;
};
typedef std::unique_ptr<IoTransportCallbacks> IoTransportCallbacksPtr;

class IoTransport {
public:
  virtual ~IoTransport() {}

  virtual void setTransportCallbacks(IoTransportCallbacks& callbacks) PURE;
  virtual std::string protocol() const PURE;
 
  virtual bool canFlushClose() PURE;
  virtual void close(Network::ConnectionEvent event) PURE;
 
  virtual IoResult doRead(Buffer::Instance& buffer) PURE;
  virtual IoResult doWrite(Buffer::Instance& buffer, bool end_stream) PURE;
  virtual void onConnected() PURE;
};
typedef std::unique_ptr<IoTransport> IoTransportPtr;


class IoTransportFactory {
public:
  virtual ~IoTransportFactory() {}

  virtual bool implementsSecureTransport() const PURE;
  virtual IoTransportPtr createTransport() const PURE;
};
typedef std::unique_ptr<IoTransportFactory> IoTransportFactoryPtr;


Definitions of IoTransport for sockets (in a file in envoy/source/common/network/io_transport_socket.h?):

class IoTransportSocket : public IoTransport {
public:
  virtual ~IoTransportSocket() {}
  virtual const Ssl::Connection* ssl() const PURE;
};
typedef std::unique_ptr<IoTransportSocket> IoTransportSocketPtr;

And then, for example, in envoy/source/common/network/raw_buffer_socket.h;

class RawBufferSocket : public IoTransportSocket,
                  protected Logger::Loggable<Logger::Id::connection> {
public:
  void setTransportCallbacks(IoTransportCallbacks& callbacks) override;
  std::string protocol() const override;
  bool canFlushClose() override { return true; }
  void close(Network::ConnectionEvent) override {}
  void onConnected() override;
  IoResult doRead(Buffer::Instance& buffer) override;
  IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  const Ssl::Connection* ssl() const override { return nullptr; }

private:
  IoTransportSocketCallbacks* callbacks_{};
  bool shutdown_{};
};
typedef std::unique_ptr<RawBufferSocket> RawBufferSocketPtr;


###### Further Notes

We anticipate that these proposed changes will require changes to the current existing classes:

Socket
SocketImpl (+derivatives)
TransportSocketFactory (+derivatives)
TransportSocket (+derivatives)
TransportSocketCallbacks (+derivatives)
RawBufferSocket
FilterManager (+derivatives)
FilterChainManager (+derivatives)
FilterChainFactory (+derivatives)
FilterChain (+derivatives)
Connection (+derivatives)
ConnectionImpl (+derivatives)
Listener (+derivatives)
ListenerImpl
AddressImpl
InstanceBase
InstanceIpv4
InstanceIpv6
IpHelper

2. Listener code and connection_handler code that moves newly-created sockets and transport sockets will
change to do so with IoHandles. A few examples:
        
        a.  In ConnectionHandlerImpl::ActiveListener::newConnection:

```

        Network::ConnectionPtr new_connection = parent_.dispatcher_.createServerConnection(std::move(ioHandle)); 

```

Note that an IoTransportPtr is not passed in because the IoTransport is contained in the IoHandle.

```

      b.  FilterChainManager::findFilterChain(const Network::ConnectionSocket& socket) will have to be modified to
          use IoHandle methods:
      
      findFilterChain(const Network::IoHandle& ioHandle) will have to use IoHandle methods, such as:

      const auto filter_chain = config_.filterChainManager().findFilterChain(*ioHandle); 

```


