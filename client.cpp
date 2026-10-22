#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <sys/types.h>
#include <cstring>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <net/if.h>

#include "group.h"
#include "SSL.h"

#define BUFSIZE 1024

using namespace std;

vector<Group> groups;
vector<IPv6Group> groups6;

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "Invalid arguments. Usage: " << argv[0] << " <hostname> <port>" << endl;
        exit(-1);
    }
    // argv[1] is usually localhost
    const char *hostName = argv[1], *serPort = argv[2];
    int port = atoi(argv[2]);
    char buffer[BUFSIZE];
    if (port == 12345)
    {
        SSLArgs sslArgs;
        if (argc >= 4 && strcmp(argv[3], "--ssl") == 0)
        {
            sslArgs.enabled = true;
        }
        struct addrinfo hints;
        struct addrinfo *result;
        struct addrinfo *rp;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        int s = getaddrinfo(hostName, serPort, &hints, &result);
        if (s != 0)
        {
            // gai_strerror(s) converts the error given by getaddrinfo() into a string.
            cerr << "getaddrinfo(): " << gai_strerror(s) << endl;
            return -1;
        }
        int sockfd = -1;
        for (rp = result; rp != NULL; rp = rp->ai_next)
        {
            sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sockfd < 0)
                continue;
            if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            {
                break;
            }
            close(sockfd);
            sockfd = -1;
        }
        if (sockfd < 0)
        {
            cerr << "Could not connect to server" << endl;
            freeaddrinfo(result);
            return -1;
        }
        freeaddrinfo(result);
        SSL_CTX *ctx = nullptr;
        SSL *ssl = nullptr;
        if (sslArgs.enabled)
        {
            SSLInit();
            ctx = SSLcreateclient(sslArgs);
            if (!ctx)
            {
                close(sockfd);
                return 1;
            }
            ssl = sslClientConnect(ctx, sockfd);
            if (!ssl)
            {
                SSL_CTX_free(ctx);
                close(sockfd);
                return -1;
            }
        }
        while (true)
        {
            cout << "Enter message: ";
            cout.flush();
            if (fgets(buffer, BUFSIZE, stdin) == NULL)
                break;

            buffer[strcspn(buffer, "\n")] = '\0';

            if (strcmp(buffer, "quit") == 0)
                break;

            if (sslArgs.enabled)
            {
                ssize_t s_size = SSL_write(ssl, buffer, strlen(buffer));
                if (s_size < 0)
                {
                    perror("send() failed");
                    break;
                }

                ssize_t r_size = SSL_read(ssl, buffer, BUFSIZE);
                if (r_size <= 0)
                {
                    perror("recv() failed");
                    break;
                }

                cout << "TLS echo: ";
                cout.write(buffer, r_size);
                cout << endl;
            }
            else
            {
                ssize_t s_size = send(sockfd, buffer, strlen(buffer), 0);
                if (s_size < 0)
                {
                    perror("send() failed");
                    break;
                }

                ssize_t r_size = recv(sockfd, buffer, BUFSIZE, 0);
                if (r_size <= 0)
                {
                    perror("recv() failed");
                    break;
                }

                cout << "TCP echo: ";
                cout.write(buffer, r_size);
                cout << endl;
            }
        }

        if (sslArgs.enabled)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
        }

        close(sockfd);
        return 0;
    }
    else if (port == 12346)
    {
        int controlSock = socket(AF_INET, SOCK_STREAM, 0);
        if (controlSock < 0)
        {
            perror("control socket");
            return -1;
        }
        struct sockaddr_in controlAddr{};
        controlAddr.sin_family = AF_INET;
        controlAddr.sin_port = htons(12344);
        if (inet_pton(AF_INET, "127.0.0.1", &controlAddr.sin_addr) <= 0)
        {
            perror("inet_pton");
            close(controlSock);
            return -1;
        }
        if (connect(controlSock, (struct sockaddr *)&controlAddr, sizeof(controlAddr)) < 0)
        {
            perror("control connect");
            close(controlSock);
            return -1;
        }
        cout << "Connected to group control server on port 12344" << endl;
        while (true)
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            int maxfd = STDIN_FILENO;
            for (auto &g : groups)
            {
                if (g.joined)
                {
                    FD_SET(g.socketFd, &readfds);
                    if (g.socketFd > maxfd)
                        maxfd = g.socketFd;
                }
            }
            int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
            if (ready < 0)
            {
                perror("select() failed");
                break;
            }
            for (auto &g : groups)
            {
                if (g.joined && FD_ISSET(g.socketFd, &readfds))
                {
                    ssize_t m_size4 = recvfrom(g.socketFd, buffer, BUFSIZE, 0, NULL, NULL);
                    if (m_size4 < 0)
                    {
                        perror("recvfrom() failed");
                        continue;
                    }
                    cout << "\nIPv4 Group " << g.groupNo << " received: ";
                    cout.write(buffer, m_size4);
                    cout << endl;
                    cout << "Enter message: ";
                    cout.flush();
                }
            }
            if (FD_ISSET(STDIN_FILENO, &readfds))
            {
                if (fgets(buffer, BUFSIZE, stdin) == NULL)
                    break;
                buffer[strcspn(buffer, "\n")] = '\0';
                if (strcmp(buffer, "quit") == 0)
                    break;
                if (strcmp(buffer, "create") == 0)
                {
                    ssize_t sent = send(controlSock, buffer, strlen(buffer), 0);
                    if (sent < 0)
                    {
                        perror("send() to control server failed");
                        continue;
                    }
                    ssize_t received = recv(controlSock, buffer, BUFSIZE - 1, 0);
                    if (received <= 0)
                    {
                        cerr << "Group control server disconnected" << endl;
                        continue;
                    }
                    buffer[received] = '\0';
                    int groupNo;
                    int groupPort;
                    int ipv6Port;
                    if (sscanf(buffer, "CREATED %d %d %d", &groupNo, &groupPort, &ipv6Port) == 3)
                    {
                        Group g;
                        g.groupNo = groupNo;
                        g.port = groupPort;
                        g.socketFd = -1;
                        g.joined = false;
                        groups.push_back(g);
                        cout << "Created group " << groupNo << " on Ipv4 port " << groupPort << " on Ipv6 port " << ipv6Port << endl;
                    }
                    else
                    {
                        cout << "Server: " << buffer << endl;
                    }
                    continue;
                }
                else if (strncmp(buffer, "join ", 5) == 0)
                {
                    int groupNo = stoi(buffer + 5);
                    bool alreadyJoined = false;
                    for (auto &g : groups)
                    {
                        if (g.groupNo == groupNo && g.joined)
                        {
                            alreadyJoined = true;
                            break;
                        }
                    }
                    if (alreadyJoined)
                    {
                        cout << "Already joined group " << groupNo << endl;
                        continue;
                    }
                    send(controlSock, buffer, strlen(buffer), 0);
                    char response[BUFSIZE];
                    int r_size = recv(controlSock, response, BUFSIZE - 1, 0);
                    if (r_size <= 0)
                    {
                        cerr << "Control server disconnected" << endl;
                        break;
                    }
                    response[r_size] = '\0';
                    string reply(response);
                    if (reply.rfind("ERROR", 0) == 0)
                    {
                        cout << reply << endl;
                        continue;
                    }
                    int returnedGroupNo;
                    int groupPort;
                    int ipv6Port;
                    if (sscanf(response, "JOINED %d %d %d", &returnedGroupNo, &groupPort, &ipv6Port) != 3)
                    {
                        cout << "Invalid response from control server" << endl;
                        continue;
                    }
                    int multicastSock = socket(AF_INET, SOCK_DGRAM, 0);
                    if (multicastSock < 0)
                    {
                        perror("socket");
                        continue;
                    }
                    int reuse = 1;
                    setsockopt(multicastSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                    sockaddr_in localAddr{};
                    localAddr.sin_family = AF_INET;
                    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                    localAddr.sin_port = htons(groupPort);
                    if (bind(multicastSock, (sockaddr *)&localAddr, sizeof(localAddr)) < 0)
                    {
                        perror("bind");
                        close(multicastSock);
                        continue;
                    }
                    ip_mreq mreq{};
                    inet_pton(AF_INET, "239.0.0.1", &mreq.imr_multiaddr);
                    // htonl(): Host TO Network Long: It converts a 32-bit integer from your computer's host byte order to network byte order.
                    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                    // multicastsock is the socket I want to configure.
                    // IPPROTO_IP: I'm setting an option related to the IPv4 protocol.
                    // IP_ADD_MEMBERSHIP: Add this socket/host to an IPv4 multicast group.
                    // mreq tells the OS which multicast group to join and which local interface to use.
                    // For this IPv4 UDP socket, at the IP protocol level, add it to the multicast group described by mreq.
                    if (setsockopt(multicastSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
                    {
                        perror("IP_ADD_MEMBERSHIP");
                        close(multicastSock);
                        continue;
                    }
                    bool found = false;
                    for (auto &g : groups)
                    {
                        if (g.groupNo == returnedGroupNo)
                        {
                            g.port = groupPort;
                            g.socketFd = multicastSock;
                            g.joined = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        Group newGroup;
                        newGroup.groupNo = returnedGroupNo;
                        newGroup.port = groupPort;
                        newGroup.socketFd = multicastSock;
                        newGroup.joined = true;
                        groups.push_back(newGroup);
                    }
                    cout << "Joined group " << returnedGroupNo << " on port " << groupPort << endl;
                }
                else if (strncmp(buffer, "leave ", 6) == 0)
                {
                    int groupNo = stoi(buffer + 6);
                    Group *group = nullptr;
                    for (auto &g : groups)
                    {
                        if (g.groupNo == groupNo)
                        {
                            group = &g;
                            break;
                        }
                    }
                    if (group == nullptr || !group->joined)
                    {
                        cout << "Not joined to group " << groupNo << endl;
                        continue;
                    }
                    send(controlSock, buffer, strlen(buffer), 0);
                    char response[BUFSIZE];
                    int r_size = recv(controlSock, response, BUFSIZE - 1, 0);
                    if (r_size <= 0)
                    {
                        cerr << "Control server disconnected" << endl;
                        break;
                    }
                    response[r_size] = '\0';
                    if (strncmp(response, "LEFT", 4) != 0)
                    {
                        cout << response << endl;
                        continue;
                    }
                    ip_mreq mreq{};
                    inet_pton(AF_INET, "239.0.0.1", &mreq.imr_multiaddr);
                    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                    if (setsockopt(group->socketFd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
                    {
                        perror("IP_DROP_MEMBERSHIP");
                    }
                    close(group->socketFd);
                    group->socketFd = -1;
                    group->joined = false;
                    cout << "Left group " << groupNo << endl;
                }
                else
                {
                    bool sent = false;
                    for (auto &g : groups)
                    {
                        if (!g.joined)
                            continue;
                        // This structure describes where we're sending the UDP packet.
                        struct sockaddr_in multicastAddr4;
                        memset(&multicastAddr4, 0, sizeof(multicastAddr4));
                        multicastAddr4.sin_family = AF_INET;
                        multicastAddr4.sin_port = htons(g.port);
                        inet_pton(AF_INET, "239.0.0.1", &multicastAddr4.sin_addr);
                        ssize_t m_size4 = sendto(g.socketFd, buffer, strlen(buffer), 0, (struct sockaddr *)&multicastAddr4,
                                                 sizeof(multicastAddr4));
                        if (m_size4 < 0)
                        {
                            perror("sendto() failed");
                            continue;
                        }
                        cout << "Sent " << m_size4 << " bytes to IPv4 group " << g.groupNo << endl;
                        sent = true;
                    }
                    if (!sent)
                    {
                        cout << "You have not joined any groups." << endl;
                    }
                }
            }
        }
        for (auto &g : groups)
        {
            if (g.joined)
            {
                close(g.socketFd);
                g.socketFd = -1;
                g.joined = false;
            }
        }
        return 0;
    }
    else if (port == 12347)
    {
        int controlSock = socket(AF_INET, SOCK_STREAM, 0);
        if (controlSock < 0)
        {
            perror("control socket");
            return -1;
        }
        struct sockaddr_in controlAddr{};
        controlAddr.sin_family = AF_INET;
        controlAddr.sin_port = htons(GROUP_CONTROL_PORT);
        if (inet_pton(AF_INET, "127.0.0.1", &controlAddr.sin_addr) <= 0)
        {
            perror("inet_pton");
            close(controlSock);
            return -1;
        }
        if (connect(controlSock, (struct sockaddr *)&controlAddr, sizeof(controlAddr)) < 0)
        {
            perror("control connect");
            close(controlSock);
            return -1;
        }
        cout << "Connected to group control server on port " << GROUP_CONTROL_PORT << endl;
        unsigned int interfaceIndex = if_nametoindex("eth0");
        if (interfaceIndex == 0)
        {
            perror("if_nametoindex");
            close(controlSock);
            return -1;
        }
        while (true)
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            int maxfd = STDIN_FILENO;
            for (auto &g : groups6)
            {
                if (g.joined)
                {
                    FD_SET(g.socketFd, &readfds);
                    if (g.socketFd > maxfd)
                        maxfd = g.socketFd;
                }
            }
            int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
            if (ready < 0)
            {
                perror("select() failed");
                break;
            }
            for (auto &g : groups6)
            {
                if (g.joined && FD_ISSET(g.socketFd, &readfds))
                {
                    ssize_t m_size6 = recvfrom(g.socketFd, buffer, BUFSIZE, 0, NULL, NULL);
                    if (m_size6 < 0)
                    {
                        perror("recvfrom() failed");
                        continue;
                    }
                    cout << "\nIPv6 Group " << g.groupNo << " received: ";
                    cout.write(buffer, m_size6);
                    cout << endl;
                    cout << "Enter message: ";
                    cout.flush();
                }
            }
            if (FD_ISSET(STDIN_FILENO, &readfds))
            {
                if (fgets(buffer, BUFSIZE, stdin) == NULL)
                    break;
                buffer[strcspn(buffer, "\n")] = '\0';
                if (strcmp(buffer, "quit") == 0)
                    break;
                if (strcmp(buffer, "create") == 0)
                {
                    send(controlSock, buffer, strlen(buffer), 0);
                    char response[BUFSIZE];
                    ssize_t r_size = recv(controlSock, response, BUFSIZE - 1, 0);
                    if (r_size <= 0)
                    {
                        cerr << "Group control server disconnected" << endl;
                        break;
                    }
                    response[r_size] = '\0';
                    int gno;
                    int ipv4Port;
                    int ipv6Port;
                    if (sscanf(response, "CREATED %d %d %d", &gno, &ipv4Port, &ipv6Port) == 3)
                    {
                        IPv6Group g;
                        g.groupNo = gno;
                        g.port = ipv6Port;
                        g.socketFd = -1;
                        g.joined = false;
                        groups6.push_back(g);
                        cout << "Created IPv6 group " << gno << " on port " << ipv6Port << endl;
                    }
                    else
                    {
                        cout << "Server: " << response << endl;
                    }
                    continue;
                }
                else if (strncmp(buffer, "join ", 5) == 0)
                {
                    int groupNo = stoi(buffer + 5);
                    bool alreadyJoined = false;
                    for (auto &g : groups6)
                    {
                        if (g.groupNo == groupNo && g.joined)
                        {
                            alreadyJoined = true;
                            break;
                        }
                    }
                    if (alreadyJoined)
                    {
                        cout << "Already joined IPv6 group " << groupNo << endl;
                        continue;
                    }
                    char request[BUFSIZE];
                    // Format the string "join <group number>" and store it in request.
                    snprintf(request, BUFSIZE, "join %d", groupNo);
                    if (send(controlSock, request, strlen(request), 0) < 0)
                    {
                        perror("send() to control server failed");
                        continue;
                    }
                    char response[BUFSIZE];
                    ssize_t r_size = recv(controlSock, response, BUFSIZE - 1, 0);
                    if (r_size <= 0)
                    {
                        cerr << "Group control server disconnected" << endl;
                        break;
                    }
                    response[r_size] = '\0';
                    if (strncmp(response, "ERROR", 5) == 0)
                    {
                        cout << response << endl;
                        continue;
                    }
                    int returnedGroupNo;
                    int ipv4Port;
                    int ipv6Port;
                    if (sscanf(response, "JOINED %d %d %d", &returnedGroupNo, &ipv4Port, &ipv6Port) != 3)
                    {
                        cout << "Invalid response from control server" << endl;
                        continue;
                    }
                    int multicast6 = socket(AF_INET6, SOCK_DGRAM, 0);
                    if (multicast6 < 0)
                    {
                        perror("IPv6 socket");
                        continue;
                    }
                    int reuse6 = 1;
                    // SOL_SOCKET: I want to set an option belonging to the socket itself.
                    // SO_REUSEADDR: Allow this socket to reuse a local address/port.
                    if (setsockopt(multicast6, SOL_SOCKET, SO_REUSEADDR, &reuse6, sizeof(reuse6)) < 0)
                    {
                        perror("SO_REUSEADDR");
                        close(multicast6);
                        continue;
                    }
                    // Create an IPv6 socket-address structure called local6 and initialize all its fields to zero.
                    // sockaddr_in6: This is the structure used to represent an IPv6 address + port.
                    sockaddr_in6 local6{};
                    local6.sin6_family = AF_INET6;
                    local6.sin6_addr = in6addr_any;
                    local6.sin6_port = htons(ipv6Port);
                    if (bind(multicast6, (sockaddr *)&local6, sizeof(local6)) < 0)
                    {
                        perror("IPv6 bind");
                        close(multicast6);
                        continue;
                    }
                    ipv6_mreq mreq6{};
                    inet_pton(AF_INET6, "ff02::1234", &mreq6.ipv6mr_multiaddr);
                    mreq6.ipv6mr_interface = interfaceIndex;
                    if (setsockopt(multicast6, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, sizeof(mreq6)) < 0)
                    {
                        perror("IPV6_JOIN_GROUP");
                        close(multicast6);
                        continue;
                    }
                    bool found = false;
                    for (auto &g : groups6)
                    {
                        if (g.groupNo == returnedGroupNo)
                        {
                            g.port = ipv6Port;
                            g.socketFd = multicast6;
                            g.joined = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        IPv6Group newGroup;
                        newGroup.groupNo = returnedGroupNo;
                        newGroup.port = ipv6Port;
                        newGroup.socketFd = multicast6;
                        newGroup.joined = true;
                        groups6.push_back(newGroup);
                    }
                    cout << "Joined IPv6 group " << returnedGroupNo << " on port " << ipv6Port << endl;
                }
                else if (strncmp(buffer, "leave ", 6) == 0)
                {
                    int groupNo = stoi(buffer + 6);
                    IPv6Group *group = nullptr;
                    for (auto &g : groups6)
                    {
                        if (g.groupNo == groupNo)
                        {
                            group = &g;
                            break;
                        }
                    }
                    if (group == nullptr || !group->joined)
                    {
                        cout << "Not joined to IPv6 group " << groupNo << endl;
                        continue;
                    }
                    char request[BUFSIZE];
                    // Format the string "leave <group number>" and store it in request.
                    snprintf(request, BUFSIZE, "leave %d", groupNo);
                    if (send(controlSock, request, strlen(request), 0) < 0)
                    {
                        perror("send() to control server failed");
                        continue;
                    }
                    char response[BUFSIZE];
                    ssize_t r_size = recv(controlSock, response, BUFSIZE - 1, 0);
                    if (r_size <= 0)
                    {
                        cerr << "Group control server disconnected" << endl;
                        break;
                    }
                    response[r_size] = '\0';
                    if (strncmp(response, "LEFT", 4) != 0)
                    {
                        cout << response << endl;
                        continue;
                    }
                    ipv6_mreq mreq6{};
                    inet_pton(AF_INET6, "ff02::1234", &mreq6.ipv6mr_multiaddr);
                    mreq6.ipv6mr_interface = interfaceIndex;
                    if (setsockopt(group->socketFd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq6, sizeof(mreq6)) < 0)
                    {
                        perror("IPV6_LEAVE_GROUP");
                    }
                    close(group->socketFd);
                    group->socketFd = -1;
                    group->joined = false;
                    cout << "Left IPv6 group " << groupNo << endl;
                }
                else
                {
                    bool sent = false;
                    for (auto &g : groups6)
                    {
                        if (!g.joined)
                            continue;
                        sockaddr_in6 multicastAddr6{};
                        multicastAddr6.sin6_family = AF_INET6;
                        multicastAddr6.sin6_port = htons(g.port);
                        // inet_pton() converts an IP address from presentation format, such as "ff02::1234", into the binary network format
                        // required by socket structures. AF_INET6 specifies that the address is IPv6.
                        inet_pton(AF_INET6, "ff02::1234", &multicastAddr6.sin6_addr);
                        multicastAddr6.sin6_scope_id = interfaceIndex;
                        ssize_t m_size6 = sendto(g.socketFd, buffer, strlen(buffer), 0, (sockaddr *)&multicastAddr6, sizeof(multicastAddr6));
                        if (m_size6 < 0)
                        {
                            perror("IPv6 sendto()");
                            continue;
                        }
                        cout << "Sent " << m_size6 << " bytes to IPv6 group " << g.groupNo << endl;
                        sent = true;
                    }
                    if (!sent)
                    {
                        cout << "You have not joined any IPv6 groups." << endl;
                    }
                }
            }
        }
        for (auto &g : groups6)
        {
            if (g.joined)
            {
                ipv6_mreq mreq6{};
                inet_pton(AF_INET6, "ff02::1234", &mreq6.ipv6mr_multiaddr);
                mreq6.ipv6mr_interface = interfaceIndex;
                setsockopt(g.socketFd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq6, sizeof(mreq6));
                close(g.socketFd);
                g.socketFd = -1;
                g.joined = false;
            }
        }
        close(controlSock);
        return 0;
    }
    else
    {
        cerr << "Invalid port" << endl;
        return -1;
    }
}