#include "protocol.h"

static volatile sig_atomic_t running = 1;
static void onSigint(int) {
    running = 0;
}

static int parsePort(const char* s){
    char *end;
    long v = std::strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < 1 || v > 65535) return -1;
    return (int)v;
}

static bool sendAll(int fd, const uint8_t *buf, int len){
    int sent = 0;
    while (sent < len){
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += (int)n;
    }
    return true;
}


int main(int argc, char *argv[]){

    if (argc < 3 || argc > 4){
        std::cerr << "Usage: " << argv[0] << " <ON_IP>[:ON_PORT] <UDP_PORT> [OWN_IP]\n";
        return 1;
    }
    signal(SIGINT, onSigint);


    // Parsing and getting the IP address and/or port of Oracle node
    char onIpStr[64];
    onIpStr[sizeof(onIpStr) - 1] = '\0';
    int onPort = ON_PORT;
    strncpy(onIpStr, argv[1], sizeof(onIpStr) - 1);
    char *colon = strchr(onIpStr, ':');

    if (colon){
        *colon = '\0';
        onPort = parsePort(colon + 1);
        if (onPort < 0){
            std::cerr << "ON port given is invalid " << *(colon + 1) << std::endl;
            return 1;
        }
    }

    sockaddr_in onAddr;
    std::memset(&onAddr, 0, sizeof(onAddr));
    onAddr.sin_family = AF_INET;
    onAddr.sin_port = htons((uint16_t)onPort);
    if (inet_pton(AF_INET, onIpStr, &onAddr.sin_addr) != 1){
        std::cerr << "ON IP address given is invalid " << onIpStr << std::endl;
        return 1;
    }

    // Getting its own UDP port
    int udpPort = parsePort(argv[2]);
    if (udpPort < 0){
        std::cerr << "UDP port given is invalid " << argv[2] << std::endl;
        return 1;
    }

    // Open and bind the UDP socket for connection to other VNs
    int udpFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpFd < 0) {
        perror("udp socket");
        return 1;
    }

    sockaddr_in udpAddr;
    memset(&udpAddr, 0, sizeof(udpAddr));
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    udpAddr.sin_port = htons((uint16_t)udpPort);
    if (bind(udpFd, (sockaddr *)&udpAddr, sizeof(udpAddr)) < 0) {
        perror("udp bind");
        close(udpFd);
        return 1;
    }

    // make the TCP connnection to the oraccle node
    int tcpFd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpFd < 0){
        perror("tcp socket");
        close(udpFd);
        return 1;
    }
    if (connect(tcpFd, (sockaddr *)&onAddr, sizeof(onAddr)) < 0){
        perror("connect to oracle");
        close(tcpFd); close(udpFd);
        return 1;
    }

    // getting its own IP address
    uint8_t ownIp[4];
    if (argc == 4){
        struct in_addr a;
        if (inet_pton(AF_INET, argv[3], &a) != 1){
            std::cerr << "own IP is invalid " << argv[3] << std::endl;
            close(tcpFd); close(udpFd);
            return 1;
        }
        std::memcpy(ownIp, &a, 4);
    } else {
        sockaddr_in local;
        socklen_t len = sizeof(local);
        if(getsockname(tcpFd, (sockaddr *)&local, &len) < 0){
            perror("getsockname");
            close(tcpFd); close(udpFd);
            return 1;
        }
        std::memcpy(ownIp, &local.sin_addr, 4);
    }
    if (ownIp[0] == 127 || (ownIp[0] | ownIp[1] | ownIp[2] | ownIp[3]) == 0){
        std::cerr << "own IP is loopback/0.0.0.0 invalid - use a real address pls\n";
        close(tcpFd); close(udpFd);
        return 1;
    }

    char ownIPstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, ownIp, ownIPstr, sizeof(ownIPstr));


    // sending CONNECT message to oracle boi
    uint8_t out[CONNECT_FRAME_LEN];
    int outLen = buildConnect(out, ownIp, (uint16_t)udpPort);

    if (!sendAll(tcpFd, out, outLen)) {
        perror("send CONNECT");
        close(tcpFd); close(udpFd);
        return 1;
    }

    std::printf("Connected to ON %s:%d, sent CONNECT (%s:%d)\n", onIpStr, onPort, ownIPstr, udpPort);

    // waiting on the TCP socket
    while (running) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(tcpFd, &rd);

        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int r = select(tcpFd + 1, &rd, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(tcpFd, &rd)) {
            uint8_t buf[512];
            ssize_t n = recv(tcpFd, buf, sizeof(buf), 0);
            if (n == 0){
                std::printf("ON closed the connection\n");
                break;
            } else if (n < 0) {
                perror("recv");
                break;
            }
            std::printf("Received %zd bytes from ON\n", n);
        }
    }

    std::printf("i'm dying bai bai\n");
    close(tcpFd);
    close(udpFd);
    return 0;
}