#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <cstdint>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <ctime>

const uint16_t ON_PORT = 5000;
const uint8_t MSG_CONNECT = 1;
const int HDR_LEN = 3; // one for message type and two for body length
const int CONNECT_BODY_LEN = 6;
const int CONNECT_FRAME_LEN = CONNECT_BODY_LEN + HDR_LEN;
const uint8_t MSG_LINK_STATE = 2;
const int TUPLE_LEN = 11; // letter (1) + ip (4) + port (2) + cost (4)
const int MAX_NODES = 26;
const int MAX_FRAME = HDR_LEN + 1 + TUPLE_LEN * MAX_NODES; // 290 bytes

const uint8_t MSG_LSP = 3;
const int LSP_HDR_LEN = 8;
const int LSP_ENTRY_LEN = 5;
const int MAX_LSP = LSP_HDR_LEN + MAX_NODES * LSP_ENTRY_LEN;
const int LSP_REPEAT = 2; // number of times to repeat each packet cuz UDP is bad :(

struct Tuple {
    char letter;
    uint8_t ip[4];
    uint16_t port;
    uint32_t cost;
};


inline void put16(uint8_t *p, uint16_t v) {
    v = htons(v);
    std::memcpy(p, &v, 2);
}

inline void put32(uint8_t *p, uint32_t v){
    v = htonl(v);
    std::memcpy(p, &v, 4);
}

inline uint16_t get16(const uint8_t *p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return ntohs(v);
}

inline uint32_t get32(const uint8_t *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return ntohl(v);
}

inline int buildConnect(uint8_t *buf, const uint8_t ip[4], uint16_t port) {
    buf[0] = MSG_CONNECT;
    put16(buf + 1, CONNECT_BODY_LEN);
    std::memcpy(buf + 3, ip, 4);
    put16(buf + 7, port);
    return CONNECT_FRAME_LEN;
} // just builds the connection frame that has to be sent to ON for doing the configs

inline int parseLinkState(const uint8_t *body, int bodyLen, Tuple out[MAX_NODES]) {
    if (bodyLen < 1) return -1;
    int k = body[0]; // contains number of tuples in the message
    if (k < 1 || k > MAX_NODES || bodyLen != 1 + TUPLE_LEN * k) return -1;

    int selfCount = 0;
    for (int i = 0; i < k; i++){
        const uint8_t *t = body + 1 + i * TUPLE_LEN;
        if (t[0] < 'A' || t[0] > 'Z') return -1;
        out[i].letter = t[0];
        std::memcpy(out[i].ip, t + 1, 4);
        out[i].port = get16(t + 5);
        out[i].cost = get32(t + 7);
        if (out[i].cost == 0) selfCount++;
    }
    return (selfCount == 1) ? k : -1;
}

inline int buildLsp(uint8_t *buf, uint8_t origin, uint8_t sender, uint32_t seq, const uint8_t *ids, const uint32_t *costs, int k){
    buf[0] = MSG_LSP;
    buf[1] = origin;
    buf[2] = sender;
    put32(buf + 3, seq);
    buf[7] = (uint8_t)k;
    for (int i = 0; i < k; i++) {
        buf[8 + i * LSP_ENTRY_LEN] = ids[i];
        put32(buf + 9 + i * LSP_ENTRY_LEN, costs[i]);
    }

    return LSP_HDR_LEN + k * LSP_ENTRY_LEN;
}

inline int checkLsp(const uint8_t *d, int len) {
    if (len < LSP_HDR_LEN || d[0] != MSG_LSP) return -1;
    if (d[1] < 'A' || d[1] > 'Z' || d[2] < 'A' || d[2] > 'Z') return -1;
    int k = d[7];
    if (k > MAX_NODES || len != LSP_HDR_LEN + k * LSP_ENTRY_LEN) return -1;
    for (int i = 0; i < k; i++) {
        const uint8_t *e = d + LSP_HDR_LEN + i * LSP_ENTRY_LEN;
        if (e[0] < 'A' || e[0] > 'Z') return -1;
        if (get32(e + 1) == 0) return -1;
    }
    return k;
}

#endif