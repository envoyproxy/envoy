.. Created by Fanbingqi on 2026-10-03.
   Purpose: Document the dual-stack IPv4 reply fix for issue #46506.
   Necessity: Users need release evidence for restored macOS UDP replies.
   Problem: IPv4 destination structures are rejected on IPv6 sockets on macOS.
   Solution and approach: Map IPv4 destinations at the Apple sendmsg boundary.

Fixed UDP replies to IPv4 clients through IPv6 dual-stack listeners on macOS, including
HTTP/3 listeners using the default UDP packet writer.
