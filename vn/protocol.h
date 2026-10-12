#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
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


inline void put16(uint8_t *p, uint16_t v) {
    v = htons(v);
    std::memcpy(p, &v, 2);
}

inline int buildConnect(uint8_t *buf, const uint8_t ip[4], uint16_t port) {
    buf[0] = MSG_CONNECT;
    put16(buf + 1, CONNECT_BODY_LEN);
    std::memcpy(buf + 3, ip, 4);
    put16(buf + 7, port);
    return CONNECT_FRAME_LEN;
} // just builds the connection frame that has to be sent to ON for doing the configs

#endif