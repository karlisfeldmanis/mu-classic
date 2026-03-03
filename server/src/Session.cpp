#include "Session.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstdio>

Session::Session(int fd) : m_fd(fd) {
    m_recvBuf.reserve(4096);
    m_sendBuf.reserve(4096);
}

Session::~Session() {
    if (m_fd >= 0) {
        close(m_fd);
    }
}

std::vector<std::vector<uint8_t>> Session::ReadPackets() {
    std::vector<std::vector<uint8_t>> packets;

    // Read from socket into recv buffer
    uint8_t tmp[4096];
    ssize_t n = recv(m_fd, tmp, sizeof(tmp), 0);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            m_alive = false;
        }
        return packets;
    }
    m_recvBuf.insert(m_recvBuf.end(), tmp, tmp + n);

    // Extract complete packets from buffer
    while (m_recvBuf.size() >= 2) {
        uint8_t type = m_recvBuf[0];
        size_t packetSize = 0;

        if (type == 0xC1 || type == 0xC3) {
            // C1/C3: size in byte 1
            packetSize = m_recvBuf[1];
        } else if (type == 0xC2 || type == 0xC4) {
            // C2/C4: size in bytes 1-2 (big-endian)
            if (m_recvBuf.size() < 3) break;
            packetSize = (static_cast<size_t>(m_recvBuf[1]) << 8) | m_recvBuf[2];
        } else {
            // Invalid packet type — skip byte
            printf("[Session] Invalid packet type 0x%02X, skipping\n", type);
            m_recvBuf.erase(m_recvBuf.begin());
            continue;
        }

        if (packetSize < 2 || packetSize > 65535) {
            printf("[Session] Invalid packet size %zu, disconnecting\n", packetSize);
            m_alive = false;
            break;
        }

        if (m_recvBuf.size() < packetSize) break; // Incomplete packet

        packets.emplace_back(m_recvBuf.begin(), m_recvBuf.begin() + packetSize);
        m_recvBuf.erase(m_recvBuf.begin(), m_recvBuf.begin() + packetSize);
    }

    return packets;
}

void Session::Send(const void *data, size_t len) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    // Try to send immediately for lower latency
    if (m_sendBuf.empty()) {
        ssize_t n = send(m_fd, bytes, len, 0);
        if (n > 0) {
            if ((size_t)n < len)
                m_sendBuf.insert(m_sendBuf.end(), bytes + n, bytes + len);
            return;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            m_alive = false;
            return;
        }
    }
    m_sendBuf.insert(m_sendBuf.end(), bytes, bytes + len);
}

bool Session::FlushSend() {
    if (m_sendBuf.empty()) return true;

    ssize_t n = send(m_fd, m_sendBuf.data(), m_sendBuf.size(), 0);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            m_alive = false;
            return false;
        }
        return true; // Would block, try later
    }
    m_sendBuf.erase(m_sendBuf.begin(), m_sendBuf.begin() + n);
    return true;
}
