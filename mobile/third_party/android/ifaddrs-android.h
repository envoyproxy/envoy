/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef IFADDRS_ANDROID_H_included
#define IFADDRS_ANDROID_H_included

#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <new>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "LocalArray.h"
#include "ScopedFd.h"

// Android (bionic) doesn't have getifaddrs(3)/freeifaddrs(3).
// We fake it here, so java_net_NetworkInterface.cpp can use that API
// with all the non-portable code being in this file.

// Source-compatible subset of the BSD struct.
struct ifaddrs {
    // Pointer to next struct in list, or NULL at end.
    ifaddrs* ifa_next;

    // Interface name.
    char* ifa_name;

    // Interface flags.
    unsigned int ifa_flags;

    // Interface network address.
    sockaddr* ifa_addr;

    // Interface netmask.
    sockaddr* ifa_netmask;

    ifaddrs(ifaddrs* next)
    : ifa_next(next), ifa_name(NULL), ifa_flags(0), ifa_addr(NULL), ifa_netmask(NULL)
    {
    }

    ~ifaddrs() {
        delete ifa_next;
        delete[] ifa_name;
        delete ifa_addr;
        delete ifa_netmask;
    }

    // Sadly, we can't keep the interface index for portability with BSD.
    // We'll have to keep the name instead, and re-query the index when
    // we need it later.
    bool setNameAndFlagsByIndex(int interfaceIndex) {
        // Get the name.
        char buf[IFNAMSIZ];
        char* name = if_indextoname(interfaceIndex, buf);
        if (name == NULL) {
            return false;
        }
        ifa_name = new char[strlen(name) + 1];
        strcpy(ifa_name, name);

        // Get the flags.
        ScopedFd fd(socket(AF_INET, SOCK_DGRAM, 0));
        if (fd.get() == -1) {
            return false;
        }
        ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strcpy(ifr.ifr_name, name);
        int rc = ioctl(fd.get(), SIOCGIFFLAGS, &ifr);
        if (rc == -1) {
            return false;
        }
        ifa_flags = ifr.ifr_flags;
        return true;
    }

    // Netlink gives us the address family in the header, and the
    // sockaddr_in or sockaddr_in6 bytes as the payload. We need to
    // stitch the two bits together into the sockaddr that's part of
    // our portable interface.
    void setAddress(int family, void* data, size_t byteCount) {
        // Set the address proper...
        sockaddr_storage* ss = new sockaddr_storage;
        memset(ss, 0, sizeof(*ss));
        ifa_addr = reinterpret_cast<sockaddr*>(ss);
        ss->ss_family = family;
        uint8_t* dst = sockaddrBytes(family, ss);
        memcpy(dst, data, byteCount);
    }

    // Netlink gives us the prefix length as a bit count. We need to turn
    // that into a BSD-compatible netmask represented by a sockaddr*.
    void setNetmask(int family, size_t prefixLength) {
        // ...and work out the netmask from the prefix length.
        sockaddr_storage* ss = new sockaddr_storage;
        memset(ss, 0, sizeof(*ss));
        ifa_netmask = reinterpret_cast<sockaddr*>(ss);
        ss->ss_family = family;
        uint8_t* dst = sockaddrBytes(family, ss);
        memset(dst, 0xff, prefixLength / 8);
        if ((prefixLength % 8) != 0) {
            dst[prefixLength/8] = (0xff << (8 - (prefixLength % 8)));
        }
    }

    // Returns a pointer to the first byte in the address data (which is
    // stored in network byte order).
    uint8_t* sockaddrBytes(int family, sockaddr_storage* ss) {
        if (family == AF_INET) {
            sockaddr_in* ss4 = reinterpret_cast<sockaddr_in*>(ss);
            return reinterpret_cast<uint8_t*>(&ss4->sin_addr);
        } else if (family == AF_INET6) {
            sockaddr_in6* ss6 = reinterpret_cast<sockaddr_in6*>(ss);
            return reinterpret_cast<uint8_t*>(&ss6->sin6_addr);
        }
        return NULL;
    }

private:
    // Disallow copy and assignment.
    ifaddrs(const ifaddrs&);
    void operator=(const ifaddrs&);
};

// FIXME: use iovec instead.
struct addrReq_struct {
    nlmsghdr netlinkHeader;
    ifaddrmsg msg;
};

inline bool sendNetlinkMessage(int fd, const void* data, size_t byteCount) {
    ssize_t sentByteCount = TEMP_FAILURE_RETRY(send(fd, data, byteCount, 0));
    return (sentByteCount == static_cast<ssize_t>(byteCount));
}

inline ssize_t recvNetlinkMessage(int fd, char* buf, size_t byteCount) {
    return TEMP_FAILURE_RETRY(recv(fd, buf, byteCount, 0));
}

// Source-compatible with the BSD function.
inline int getifaddrs(ifaddrs** result) {
    // Simplify cleanup for callers.
    *result = NULL;

    // Create a netlink socket.
    ScopedFd fd(socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE));
    if (fd.get() < 0) {
        return -1;
    }

    // Ask for the address information.
    addrReq_struct addrRequest;
    memset(&addrRequest, 0, sizeof(addrRequest));
    addrRequest.netlinkHeader.nlmsg_flags = NLM_F_REQUEST | NLM_F_MATCH;
    addrRequest.netlinkHeader.nlmsg_type = RTM_GETADDR;
    addrRequest.netlinkHeader.nlmsg_len = NLMSG_ALIGN(NLMSG_LENGTH(sizeof(addrRequest)));
    addrRequest.msg.ifa_family = AF_UNSPEC; // All families.
    addrRequest.msg.ifa_index = 0; // All interfaces.
    if (!sendNetlinkMessage(fd.get(), &addrRequest, addrRequest.netlinkHeader.nlmsg_len)) {
        return -1;
    }

    // Read the responses.
    LocalArray<0> buf(65536); // We don't necessarily have std::vector.
    ssize_t bytesRead;
    while ((bytesRead  = recvNetlinkMessage(fd.get(), &buf[0], buf.size())) > 0) {
        nlmsghdr* hdr = reinterpret_cast<nlmsghdr*>(&buf[0]);
        for (; NLMSG_OK(hdr, (size_t)bytesRead); hdr = NLMSG_NEXT(hdr, bytesRead)) {
            switch (hdr->nlmsg_type) {
            case NLMSG_DONE:
                return 0;
            case NLMSG_ERROR:
                return -1;
            case RTM_NEWADDR:
                {
                    ifaddrmsg* address = reinterpret_cast<ifaddrmsg*>(NLMSG_DATA(hdr));
                    rtattr* rta = IFA_RTA(address);
                    size_t ifaPayloadLength = IFA_PAYLOAD(hdr);
                    while (RTA_OK(rta, ifaPayloadLength)) {
                        if (rta->rta_type == IFA_ADDRESS) {
                            int family = address->ifa_family;
                            if (family == AF_INET || family == AF_INET6) {
                                *result = new ifaddrs(*result);
                                if (!(*result)->setNameAndFlagsByIndex(address->ifa_index)) {
                                    return -1;
                                }
                                (*result)->setAddress(family, RTA_DATA(rta), RTA_PAYLOAD(rta));
                                (*result)->setNetmask(family, address->ifa_prefixlen);
                            }
                        }
                        rta = RTA_NEXT(rta, ifaPayloadLength);
                    }
                }
                break;
            }
        }
    }
    // We only get here if recv fails before we see a NLMSG_DONE.
    return -1;
}

// Source-compatible with the BSD function.
inline void freeifaddrs(ifaddrs* addresses) {
    delete addresses;
}

#endif  // IFADDRS_ANDROID_H_included
