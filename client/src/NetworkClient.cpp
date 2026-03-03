#include "NetworkClient.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

NetworkClient::~NetworkClient() { Disconnect(); }

bool NetworkClient::Connect(const char *host, uint16_t port) {
  Disconnect();

  m_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_fd < 0) {
    printf("[Net] Failed to create socket\n");
    return false;
  }

  // Disable Nagle's algorithm for low latency
  int tcpNoDelay = 1;
  setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &tcpNoDelay, sizeof(tcpNoDelay));

  // Connect with a blocking call first, then switch to non-blocking
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host, &addr.sin_addr);

  // Set a brief timeout for the initial connect
  struct timeval tv{3, 0};
  setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connect(m_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    printf("[Net] Cannot connect to %s:%d (Error: %s)\n", host, port,
           strerror(errno));
    close(m_fd);
    m_fd = -1;
    return false;
  }

  // Switch to non-blocking
  int flags = fcntl(m_fd, F_GETFL, 0);
  fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

  // Clear any timeouts now that we're non-blocking
  tv = {0, 0};
  setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  m_connected = true;
  m_recvBuf.clear();
  m_sendBuf.clear();
  printf("[Net] Connected to %s:%d\n", host, port);
  return true;
}

void NetworkClient::Disconnect() {
  if (m_fd >= 0) {
    close(m_fd);
    m_fd = -1;
  }
  m_connected = false;
  m_recvBuf.clear();
  m_sendBuf.clear();
}

void NetworkClient::Poll() {
  if (!m_connected)
    return;

  // Read available data (non-blocking)
  uint8_t buf[8192];
  while (true) {
    ssize_t n = recv(m_fd, buf, sizeof(buf), 0);
    if (n > 0) {
      m_recvBuf.insert(m_recvBuf.end(), buf, buf + n);
    } else if (n == 0) {
      // Connection closed by server
      printf("[Net] Server closed connection\n");
      Disconnect();
      return;
    } else {
      // EAGAIN/EWOULDBLOCK = no more data right now
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      // Real error
      printf("[Net] recv error: %s\n", strerror(errno));
      Disconnect();
      return;
    }
  }

  // Extract complete MU packets from recv buffer
  while (m_recvBuf.size() >= 2) {
    uint8_t type = m_recvBuf[0];
    int pktSize = 0;

    if (type == 0xC1 || type == 0xC3) {
      pktSize = m_recvBuf[1];
    } else if (type == 0xC2 || type == 0xC4) {
      if (m_recvBuf.size() < 3)
        break;
      pktSize = (m_recvBuf[1] << 8) | m_recvBuf[2];
    } else {
      // Invalid packet type — skip byte
      m_recvBuf.erase(m_recvBuf.begin());
      continue;
    }

    if (pktSize < 2) {
      // Invalid packet size — skip byte
      m_recvBuf.erase(m_recvBuf.begin());
      continue;
    }
    if ((int)m_recvBuf.size() < pktSize)
      break; // Incomplete packet, wait for more data

    // Complete packet — deliver to handler
    if (onPacket) {
      onPacket(m_recvBuf.data(), pktSize);
    }

    m_recvBuf.erase(m_recvBuf.begin(), m_recvBuf.begin() + pktSize);
  }
}

void NetworkClient::Send(const void *data, size_t len) {
  if (!m_connected || len == 0)
    return;
  const uint8_t *p = static_cast<const uint8_t *>(data);

  // Try to send immediately (skip buffering for lower latency)
  if (m_sendBuf.empty()) {
    ssize_t n = send(m_fd, p, len, 0);
    if (n > 0) {
      if ((size_t)n < len)
        m_sendBuf.insert(m_sendBuf.end(), p + n, p + len);
      return;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      printf("[Net] send error: %s\n", strerror(errno));
      Disconnect();
      return;
    }
  }
  // Buffer if direct send didn't work or there's pending data
  m_sendBuf.insert(m_sendBuf.end(), p, p + len);
}

void NetworkClient::Flush() {
  if (!m_connected || m_sendBuf.empty())
    return;

  while (!m_sendBuf.empty()) {
    ssize_t n = send(m_fd, m_sendBuf.data(), m_sendBuf.size(), 0);
    if (n > 0) {
      m_sendBuf.erase(m_sendBuf.begin(), m_sendBuf.begin() + n);
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break; // Would block, try again next frame
      printf("[Net] send error: %s\n", strerror(errno));
      Disconnect();
      return;
    }
  }
}
