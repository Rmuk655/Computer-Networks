# Networked Messaging System

A socket-based messaging system prototype implemented in C++ using TCP, UDP multicast, IPv4, IPv6, hostname resolution, SSL/TLS, timestamps, and group management.

## Overview

The system consists of a server and multiple clients. Communication is divided into two parts:

* **TCP control part:** Handles group-management operations such as creating, joining, and leaving groups. A separate TCP service is also provided for direct client-server echo communication and SSL/TLS testing.
* **UDP multicast data part:** Handles sending and receiving messages within multicasted groups.

The system supports both IPv4 and IPv6 TCP + multicast. An IPv4 client and an IPv6 client can be part of the same logical group, but their multicast messages are sent through separate IPv4 and IPv6 multicast channels.

## Features

* TCP client-server communication
* Message timestamps
* Hostname resolution using `getaddrinfo()`
* SSL/TLS-secured TCP communication
* `select()`-based multiplexing of multiple sockets
* IPv4 UDP multicast
* IPv6 UDP multicast
* Group creation, joining, and leaving
* Dynamic multicast ports for groups

## Architecture

### Control Part

The group-control server listens on the TCP port numbered `12344` (hardcoded).

Clients can send the following commands:

```text
create
join N
leave N
```

When a new group is created, the server returns:

```text
CREATED <groupNo> <ipv4Port> <ipv6Port>
```

For a join request, the response is:

```text
JOINED <groupNo> <ipv4Port> <ipv6Port>
```

A successful leave operation returns:

```text
LEFT
```

Each logical group is assigned separate IPv4 and IPv6 multicast ports. For example, group 1 uses ports 12346 (IPv4) and 12347 (IPv6). The server returns these ports to clients when a group is created or joined.

## Directory Structure

```text
CS24BTECH11034_CS24BTECH11036/
├── server.cpp
├── client.cpp
├── group.h
├── SSL.h
├── server.crt
├── server.key
├── README.md
└── report.pdf

```

## Requirements

* Linux environment (WSL/Linux)
* C++17 compiler
* OpenSSL development libraries

## Compilation

Compile the server using the following command:

```bash
g++ -std=c++17 -Wall -Wextra -O2 server.cpp -o server -lssl -lcrypto -pthread
```

Compile the client using the following command::

```bash
g++ -std=c++17 -Wall -Wextra -O2 client.cpp -o client -lssl -lcrypto -pthread
```

### Install dependencies and create a TLS certificate

```bash
sudo apt install build-essential libssl-dev     # Debian / Ubuntu
openssl req -x509 -newkey rsa:2048 -nodes -keyout server.key -out server.crt -days 365 -subj "/CN=localhost"
```

The private key `server.key` is deliberately not committed, so generate a key and certificate pair before using TLS (this replaces the sample `server.crt`).

## Running the System

### 1. Start the group-control server

```bash
./server 12344
```

The server then listens on port `12344` for group-management requests.

### 2. Start an IPv4 client

```bash
./client localhost 12346
```

### 3. Start an IPv6 client

```bash
./client localhost 12347
```

Multiple clients can be started at the same time to test multicast communication.

## Client Commands

### Create a group

```text
create
```

The server creates a new logical group and returns its multicast information.

### Join a group

```text
join N
```

Here, `N` is the group number.

For example:

```text
join 1
```

### Leave a group

```text
leave N
```

For example:

```text
leave 1
```

### Send a message

Once a client has joined a group, entering any text sends that text message to all clients that have joined the same multicast group using the same protocol.

### Quit

```text
quit
```

## IPv6 Multicast

IPv6 clients use:

```text
ff02::1234
```

## SSL/TLS

SSL/TLS support is implemented using the OpenSSL library.
SSL/TLS can be enabled on the TCP echo service on port 12345 using the `--ssl` option.

```bash
./server 12345 --ssl
./client localhost 12345 --ssl
```

## Hostname Resolution

The client uses `getaddrinfo()`.

## Testing

We tested the system for the following cases:

* IPv4 multicast communication
* IPv6 multicast communication
* Communication between multiple clients
* SSL/TLS communication
* Group creation
* Joining and leaving groups
* IPv4 and IPv6 clients belonging to the same logical group

## Limitations

### Hardcoded IPv6 Interface

The IPv6 multicast implementation hardcodes the network interface used as:

```text
eth0
```

The interface index is obtained using:

```cpp
if_nametoindex("eth0")
```

Due to this, IPv6 multicast needs to be modified on systems with different network interface names, like `wlan0`, `enp0s3`, etc. You can find this using the bash command 
```bash 
ip a  
```

### Local Control Connection

The clients currently connect to the group-control server using the local IPv4 address 127.0.0.1. Therefore, the current implementation is intended to run the clients and control server on the same machine.

## Authors

**Krishnan R** — CS24BTECH11036

**Praneeth Kodavati** — CS24BTECH11034

## Debugging notes

Capturing DNS traffic while testing hostname resolution:

```bash
sudo tcpdump -i any -nn -vv 'port 53'
```

- `sudo`: packet capture needs root privileges.
- `tcpdump`: command-line packet capture and analysis tool.
- `-i any`: capture packets from all network interfaces.
- `-nn`: do not resolve IP addresses or port numbers into names.
- `-vv`: very verbose output; show more packet details.
- `'port 53'`: capture only packets involving port 53, i.e. DNS traffic.
