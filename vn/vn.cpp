#include "protocol.h"

static const bool VERBOSE = false;
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

static void printNeighbourhood(const Tuple t[], int k) {
    for (int i = 0; i < k; i++) {
        if (t[i].cost == 0) std::printf("%c=0,", t[i].letter);
    }

    for (int i = 0; i < k; i++) {
        if (t[i].cost != 0) {
            std::printf("%c=%d,", t[i].letter, t[i].cost);
        }
    }
    std::printf("\n");
}

static double t0 = 0.0;
static const double LSP_PERIOD = 10.0; // time (in seconds) to wait before sending LSP again
static const double PRINT_PERIOD = 10.0; // time (in seconds) to wait before printing graph

static double nowSec() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

struct Nbr {
    bool present;
    uint8_t ip[4];
    uint16_t port;
    uint32_t cost;
};

struct LsEntry {
    bool valid;
    uint32_t seq;
    int count;
    uint8_t ids[MAX_NODES];
    uint32_t costs[MAX_NODES];
};

static void applyLinkState(const Tuple t[], int k, int &myId, Nbr nbr[]) {
    std::memset(nbr, 0, sizeof(Nbr) * MAX_NODES);
    for (int i = 0; i < k; i++) {
        int id = t[i].letter - 'A';
        if (t[i].cost == 0) {
            if (myId != -1 && myId != id) {
                std::cerr << "warning: my letter changed from " << (char)('A' + myId) << " to " << t[i].letter << std::endl;
            }
            myId = id;
        } else {
            nbr[id].present = true;
            std::memcpy(nbr[id].ip, t[i].ip, 4);
            nbr[id].port = t[i].port;
            nbr[id].cost = t[i].cost;
        }
    }
}


static void sendToNbr(int udpFd, const Nbr& n, const uint8_t *pkt, int len) {
    sockaddr_in dst;
    std::memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(n.port);
    std::memcpy(&dst.sin_addr, n.ip, 4);
    
    for (int r = 0; r < LSP_REPEAT; r++) {
        if (sendto(udpFd, pkt, len, 0, (sockaddr *)&dst, sizeof(dst)) < 0){
            perror("sendto LSP");
        }
    }
}

static void originateLsp(int udpFd, int myId, uint32_t &mySeq, const Nbr nbr[]) {
    if (myId < 0) return;
    
    uint8_t ids[MAX_NODES];
    uint32_t costs[MAX_NODES];
    int k = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (nbr[i].present) {
            ids[k] = (uint8_t)('A' + i);
            costs[k] = nbr[i].cost;
            k++;
        }
    }

    mySeq++;
    uint8_t pkt[MAX_LSP];
    uint8_t me = (uint8_t)('A' + myId);
    int len = buildLsp(pkt, me, me, mySeq, ids, costs, k);

    for (int i = 0; i < MAX_NODES; i++) {
        if (nbr[i].present) sendToNbr(udpFd, nbr[i], pkt, len);
    }
    if (VERBOSE)
        std::printf("sent LSP seq=%u (%d links)\n", (unsigned)mySeq, k);
}

static void handleLsp(int udpFd, int myId, const Nbr nbr[], LsEntry lsp[], uint8_t *pkt, int len) {
    if (myId < 0) return;

    int k = checkLsp(pkt, len);
    if (k < 0) {
        std::printf("dropped malformed LSP (%d bytes)\n", len);
        return;
    }

    int origin = pkt[1] - 'A';
    int sender = pkt[2] - 'A';
    uint32_t seq = get32(pkt + 3);

    if (origin == myId){
        if (VERBOSE)
            std::printf("ignoring my own LSP\n");
        return;
    }

    if (lsp[origin].valid && seq <= lsp[origin].seq) {
        if (VERBOSE)
            std::printf("dropped duplicate/old LSP origin=%c seq=%u\n", pkt[1], (unsigned)seq);
        return;
    }

    // newer (higher seq number) so we store it and update the node's graph
    lsp[origin].valid = true;
    lsp[origin].seq = seq;
    lsp[origin].count = k;
    for (int i = 0; i < k; i++) {
        const uint8_t *e = pkt + LSP_HDR_LEN + i * LSP_ENTRY_LEN;
        lsp[origin].ids[i] = e[0];
        lsp[origin].costs[i] = get32(e+1);
    }
    if (VERBOSE)
        std::printf("stored LSP origin=%c seq=%u (from %c)\n", pkt[1], (unsigned)seq, pkt[2]);

    // now forward it to everyone except sender and origin
    pkt[2] = (uint8_t)('A' + myId);
    for (int i = 0; i < MAX_NODES; i++) {
        if(!nbr[i].present || i == sender || i == origin) continue;
        sendToNbr(udpFd, nbr[i], pkt, len);
        if (VERBOSE)
            std::printf("  forwarded to %c\n", (char)('A' + i));
    }
}


// print what this node knows about the whole network
static void printGraph(int myId, const Nbr nbr[], const LsEntry lsp[]) {
    if (myId < 0) return;

    int cost[MAX_NODES][MAX_NODES];
    for (int i = 0; i < MAX_NODES; i++) {
        for (int j = 0; j < MAX_NODES; j++) {
            cost[i][j] = -1;
        }
    }

    int maxId = myId;

    // neighbour links
    for (int v = 0; v < MAX_NODES; v++) {
        if (!nbr[v].present) continue;
        cost[myId][v] = (int)nbr[v].cost;
        if (v > maxId) maxId = v;
    }

    // all other links
    for (int u = 0; u < MAX_NODES; u++) {
        if (!lsp[u].valid) continue;
        if (u > maxId) maxId = u;
        for (int i = 0; i < lsp[u].count; i++) {
            int v = lsp[u].ids[i] - 'A';
            cost[u][v] = (int)lsp[u].costs[i];
            if (v > maxId) maxId = v;
        }
    }

    int n = maxId + 1;
    std::printf("-- Graph at node %c, t=%.0fs --\n", 'A' + myId, nowSec() - t0);
    for (int i = 0; i < n - 1; i++) {
        for (int k = 0; k < i; k++) std::printf("\t");
        for (int j = i + 1; j < n; j++) {
            int c = (cost[i][j] != -1) ? cost[i][j] : cost[j][i];
            std::printf("%d", c);
            if (j < n - 1) std::printf("\t");
        }
        std::printf("\n");
    }
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
            std::cerr << "ON port given is invalid " << colon + 1 << std::endl;
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

    uint8_t rbuf[2 * MAX_FRAME];
    int fill = 0;
    int myId = -1;
    uint32_t mySeq = 0;
    Nbr nbr[MAX_NODES]; // stores the neighbour info
    LsEntry lsp[MAX_NODES]; // stores LSPs from each of the origins
    std::memset(nbr, 0, sizeof(nbr));
    std::memset(lsp, 0, sizeof(lsp));

    t0 = nowSec();
    double nextLsp = nowSec() + LSP_PERIOD;
    double nextPrint = nowSec() + PRINT_PERIOD;
    // waiting on both sockets with 'select' syscall
    while (running) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(tcpFd, &rd);
        FD_SET(udpFd, &rd);

        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int maxFd = (tcpFd > udpFd) ? tcpFd : udpFd;
        int r = select(maxFd + 1, &rd, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

    
        if (FD_ISSET(tcpFd, &rd)) {
            ssize_t n = recv(tcpFd, rbuf + fill, sizeof(rbuf) - fill, 0);
            if (n == 0) {
                std::printf("ON closed the connection\n");
                break;
            } else if (n < 0) {
                perror("recv");
                break;
            }
            fill += (int)n;

            bool bad = false;
            while (fill >= HDR_LEN) {
                uint8_t type = rbuf[0];
                int bodyLen = get16(rbuf + 1);

                // checking header as soon as it arrives because if corrupted then cooked if we don't check
                if (type != MSG_LINK_STATE || bodyLen > MAX_FRAME - HDR_LEN) {
                    std::cerr << "protocol error: type = " << (int)type << ", len = " << bodyLen << std::endl;
                    bad = true;
                    break;
                }

                int frameLen = HDR_LEN + bodyLen;
                if (fill < frameLen) break;

                Tuple tuples[MAX_NODES];
                int k = parseLinkState(rbuf + HDR_LEN, bodyLen, tuples);
                if (k < 0) {
                    std::cerr << "wrong LINK-STATE packet\n";
                    bad = true;
                    break;
                }
                printNeighbourhood(tuples, k);
                applyLinkState(tuples, k, myId, nbr);
                originateLsp(udpFd, myId, mySeq, nbr);

                std::memmove(rbuf, rbuf + frameLen, fill - frameLen);
                fill -= frameLen;
            }
            if (bad) break;
        }

        if (FD_ISSET(udpFd, &rd)) {
            uint8_t pkt[2048];
            ssize_t n = recvfrom(udpFd, pkt, sizeof(pkt), 0, NULL, NULL);
            if (n < 0) {
                perror("recvfrom");
            } else {
                handleLsp(udpFd, myId, nbr, lsp, pkt, (int)n);
            }
        }

        double t = nowSec();
        if (t >= nextLsp) {
            originateLsp(udpFd, myId, mySeq, nbr);
            nextLsp = t + LSP_PERIOD;
        }
        if (t >= nextPrint) {
            printGraph(myId, nbr, lsp);
            nextPrint = t + PRINT_PERIOD;
        }
    }

    std::printf("I'm going offline. Goodbye.\n");
    close(tcpFd);
    close(udpFd);
    return 0;
}