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
#include <chrono>
#include <iomanip>
#include <net/if.h>

#include "group.h"
#include "SSL.h"

#define BUFSIZE 1024
#define REQUESTS 10

using namespace std;

vector<Group> groups;
int nextGroupNo = 1;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        cerr << "Invalid arguments. Usage: " << argv[0] << " <port>" << endl;
        exit(-1);
    }
    int port = atoi(argv[1]);
    char buffer[BUFSIZE];
    if (port == 12344)
    {
        int listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd < 0)
        {
            perror("socket() failed");
            return -1;
        }
        int reuse = 1;
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(12344);
        if (bind(listenfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        {
            perror("bind() failed");
            close(listenfd);
            return -1;
        }
        if (listen(listenfd, 10) < 0)
        {
            perror("listen() failed");
            close(listenfd);
            return -1;
        }
        cout << "Group control server listening on port 12344" << endl;
        vector<int> clients;
        while (true)
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(listenfd, &readfds);
            int maxfd = listenfd;
            for (int clientfd : clients)
            {
                FD_SET(clientfd, &readfds);
                if (clientfd > maxfd)
                    maxfd = clientfd;
            }
            // Block until at least one of these sockets becomes ready for reading.
            // maxfd + 1: Examine FDs 0 through maxfd, readfds: Monitor for READ readiness, NULL: Don't monitor write readiness
            // NULL: Don't monitor exceptional conditions, NULL: Wait indefinitely.
            int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
            if (ready < 0)
            {
                perror("select() failed");
                break;
            }
            if (FD_ISSET(listenfd, &readfds))
            {
                struct sockaddr_storage clientAddr;
                socklen_t clientLen = sizeof(clientAddr);
                int clientfd = accept(listenfd, (struct sockaddr *)&clientAddr, &clientLen);
                if (clientfd < 0)
                {
                    perror("accept() failed");
                }
                else
                {
                    clients.push_back(clientfd);
                    cout << "Group control client connected. " << "FD = " << clientfd << endl;
                }
            }
            for (auto it = clients.begin(); it != clients.end();)
            {
                int clientfd = *it;
                if (!FD_ISSET(clientfd, &readfds))
                {
                    ++it;
                    continue;
                }
                ssize_t r_size = recv(clientfd, buffer, BUFSIZE - 1, 0);
                if (r_size <= 0)
                {
                    cout << "Group control client disconnected. " << "FD = " << clientfd << endl;
                    close(clientfd);
                    it = clients.erase(it);
                    continue;
                }
                buffer[r_size] = '\0';
                cout << "Control command from FD " << clientfd << ": " << buffer << endl;
                if (strcmp(buffer, "create") == 0)
                {
                    int groupNo = nextGroupNo;
                    int groupPort = IPV4_GROUP_BASE_PORT + groupNo * 2;
                    int ipv6Port = IPV6_GROUP_BASE_PORT + groupNo * 2;
                    Group g;
                    g.groupNo = groupNo;
                    g.port = groupPort;
                    g.socketFd = -1;
                    g.joined = false;
                    groups.push_back(g);
                    nextGroupNo++;
                    char response[BUFSIZE];
                    snprintf(response, BUFSIZE, "CREATED %d %d %d", groupNo, groupPort, ipv6Port);
                    send(clientfd, response, strlen(response), 0);
                }
                else if (strncmp(buffer, "join ", 5) == 0)
                {
                    int groupNo;
                    if (sscanf(buffer, "join %d", &groupNo) != 1)
                    {
                        const char *msg = "ERROR Invalid join command";
                        send(clientfd, msg, strlen(msg), 0);
                        ++it;
                        continue;
                    }
                    if (groupNo < 1 || groupNo >= nextGroupNo)
                    {
                        const char *msg = "ERROR Group not created";
                        send(clientfd, msg, strlen(msg), 0);
                        ++it;
                        continue;
                    }
                    Group &g = groups[groupNo - 1];
                    int ipv6Port = IPV6_GROUP_BASE_PORT + groupNo * 2;
                    char response[BUFSIZE];
                    snprintf(response, BUFSIZE, "JOINED %d %d %d", g.groupNo, g.port, ipv6Port);
                    send(clientfd, response, strlen(response), 0);
                }
                else if (strncmp(buffer, "leave ", 6) == 0)
                {
                    int groupNo;
                    if (sscanf(buffer, "leave %d", &groupNo) != 1)
                    {
                        const char *msg = "ERROR Invalid leave command";
                        send(clientfd, msg, strlen(msg), 0);
                        ++it;
                        continue;
                    }
                    if (groupNo < 1 || groupNo >= nextGroupNo)
                    {
                        const char *msg = "ERROR Group not created";
                        send(clientfd, msg, strlen(msg), 0);
                        ++it;
                        continue;
                    }
                    const char *msg = "LEFT";
                    send(clientfd, msg, strlen(msg), 0);
                }
                else
                {
                    const char *msg = "ERROR Unknown command";
                    send(clientfd, msg, strlen(msg), 0);
                }
                ++it;
            }
        }
        for (int clientfd : clients)
            close(clientfd);
        close(listenfd);
        return 0;
    }
    else if (port == 12345)
    {
        SSLArgs sslArgs;
        if (argc >= 3 && strcmp(argv[2], "--ssl") == 0)
        {
            sslArgs.enabled = true;
        }
        // TCP code
        // getaddrinfo() implementation
        struct addrinfo hints;
        struct addrinfo *result, *rp;
        // Prepare the hints for resolver
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;
        int s = getaddrinfo(NULL, "12345", &hints, &result);
        if (s != 0)
        {
            perror("getaddrinfo() failed");
            exit(-1);
        }
        int sock4 = -1, sock6 = -1, v6only = 1, reuse = 1;
        for (rp = result; rp != NULL; rp = rp->ai_next)
        {
            switch (rp->ai_family)
            {
            case AF_INET:
                sock4 = socket(AF_INET, SOCK_STREAM, 0);
                if (sock4 < 0)
                {
                    perror("IPv4 socket() failed");
                    exit(-1);
                }
                setsockopt(sock4, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                if (bind(sock4, rp->ai_addr, rp->ai_addrlen) < 0)
                {
                    perror("IPv4 bind() failed");
                    close(sock4);
                    sock4 = -1;
                    continue;
                }
                cout << "IPv4 bind successful" << endl;
                break;
            case AF_INET6:
                sock6 = socket(AF_INET6, SOCK_STREAM, 0);
                if (sock6 < 0)
                {
                    perror("IPv6 socket() failed");
                    exit(-1);
                }
                setsockopt(sock6, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                setsockopt(sock6, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
                if (bind(sock6, rp->ai_addr, rp->ai_addrlen) < 0)
                {
                    perror("IPv6 bind() failed");
                    close(sock6);
                    sock6 = -1;
                    continue;
                }
                cout << "IPv6 bind successful" << endl;
                break;
            };
        }
        freeaddrinfo(result);
        if (sock4 < 0 && sock6 < 0)
        {
            cerr << "Could not bind any server socket" << endl;
            exit(-1);
        }
        if (sock4 >= 0)
        {
            if (listen(sock4, REQUESTS) < 0)
            {
                perror("listen() for IPv4 failed");
                exit(-1);
            }
        }
        if (sock6 >= 0)
        {
            if (listen(sock6, REQUESTS) < 0)
            {
                perror("listen() for IPv6 failed");
                exit(-1);
            }
        }
        SSL_CTX *ctx = nullptr;
        if (sslArgs.enabled)
        {
            SSLInit();
            ctx = SSLcreateserver(sslArgs);
            if (!ctx)
            {
                cout << "Could not initialize TLS context -- check --cert/--key paths" << endl;
                exit(-1);
            }
            cout << "TLS enabled (cert=" << sslArgs.cert << " , key=" << sslArgs.key << ")" << endl;
        }
        while (true)
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            if (sock4 >= 0)
                FD_SET(sock4, &readfds);
            if (sock6 >= 0)
                FD_SET(sock6, &readfds);
            int maxfd = max(sock4, sock6);
            if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0)
            {
                perror("select failed");
                exit(-1);
            }
            struct sockaddr_storage client;
            socklen_t client_size = sizeof(client);
            int ac = -1;
            if (sock4 >= 0 && FD_ISSET(sock4, &readfds))
            {
                ac = accept(sock4, (struct sockaddr *)&client, &client_size);
            }
            else if (sock6 >= 0 && FD_ISSET(sock6, &readfds))
            {
                ac = accept(sock6, (struct sockaddr *)&client, &client_size);
            }
            if (ac < 0)
            {
                perror("accept() failed");
                continue;
            }
            SSL *ssl = nullptr;
            if (sslArgs.enabled)
            {
                ssl = sslServerAccept(ctx, ac);
                if (!ssl)
                {
                    close(ac);
                    continue;
                }
            }
            while (true)
            {
                auto now = chrono::system_clock::now();
                auto recv_start = chrono::steady_clock::now();
                ssize_t r_size = sslArgs.enabled ? SSL_read(ssl, buffer, BUFSIZE) : recv(ac, buffer, BUFSIZE, 0);
                auto recv_end = chrono::steady_clock::now();
                if (r_size == 0)
                {
                    break;
                }
                if (r_size < 0)
                {
                    perror("recv() failed");
                    break;
                }

                string recv_mes(buffer, r_size);
                auto recv_duration = chrono::duration_cast<chrono::microseconds>(recv_end - recv_start);
                time_t recv_time = chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count();

                auto send_start = chrono::steady_clock::now();
                ssize_t s_size = sslArgs.enabled ? SSL_write(ssl, buffer, r_size) : send(ac, buffer, r_size, 0);
                auto send_end = chrono::steady_clock::now();
                string send_mes(buffer, s_size);
                auto send_duration = chrono::duration_cast<chrono::microseconds>(send_end - send_start);
                time_t send_time = chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count();

                cout << "Message: ";
                cout.write(buffer, r_size);
                cout << endl;
                cout << "Time stamp of received message " << recv_mes << " : " << put_time(localtime(&recv_time), "%Y-%m-%d %H:%M:%S") << endl;
                cout << "Time stamp of sent message " << send_mes << " : " << put_time(localtime(&send_time), "%Y-%m-%d %H:%M:%S") << endl;
                cout << "Time taken to receive message " << recv_mes << " = " << recv_duration.count() << " microseconds" << endl;
                cout << "Time taken to send message " << recv_mes << " = " << send_duration.count() << " microseconds" << endl;
            }
            if (sslArgs.enabled)
            {
                SSL_shutdown(ssl);
                SSL_free(ssl);
            }
            close(ac);
        }
        if (ctx)
            SSL_CTX_free(ctx);
        if (sock4 >= 0)
            close(sock4);
        if (sock6 >= 0)
            close(sock6);
        return 0;
    }
    else
    {
        cerr << "Invalid port. Use 12344, 12345." << endl;
        return -1;
    }
}