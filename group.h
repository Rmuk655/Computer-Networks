#ifndef GROUP_h
#define GROUP_h

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

#define GROUP_CONTROL_PORT 12344
#define IPV4_GROUP_BASE_PORT 12344
#define IPV6_GROUP_BASE_PORT 12345

struct Group
{
    int groupNo;
    int port;
    int socketFd;
    bool joined;
};

struct IPv6Group
{
    int groupNo;
    int port;
    int socketFd;
    bool joined;
};

#endif