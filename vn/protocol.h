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

const uint16_t ON_PORT = 5000;
const uint8_t MSG_CONNECT = 1;
const int HDR_LEN = 3; // one for message type and two for body length
const int CONNECT_BODY_LEN = 6;
const int CONNECT_FRAME_LEN = CONNECT_BODY_LEN + HDR_LEN;
const uint8_t MSG_LINK_STATE = 2;
const int TUPLE_LEN = 11; // letter (1) + ip (4) + port (2) + cost (4)
const int MAX_NODES = 26;
const int MAX_FRAME = HDR_LEN + 1 + TUPLE_LEN * MAX_NODES; // 290 bytes

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

#endif