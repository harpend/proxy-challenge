#include "proxy.h"
#include <sys/socket.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>   
#include <arpa/inet.h> 
#include <thread>
#include <vector>
#include <mutex>
#include <string.h>
#include <cstddef>
#include <signal.h>
#include <poll.h>
#include <immintrin.h>

#define DEBUG 0

#if DEBUG
  #define DEBUG_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(fmt, ...)
#endif

Proxy::Proxy(int ingress_port, int egress_port) : ingress_port(ingress_port), egress_port(egress_port) {}

// creates a sockets, then sets it to listen
int Proxy::createSocket(int port, int queue_size, sockaddr_in* sockaddr) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        throw std::runtime_error("ingress socket failed");
    }
    
    *sockaddr = {};
    sockaddr->sin_family = AF_INET;
    sockaddr->sin_port = htons(port);
    sockaddr->sin_addr.s_addr =  inet_addr("127.0.0.1");
    int opt = 1;
    if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("failed to set socket options");
    }

    if (bind(socket_fd, (struct sockaddr*)sockaddr, sizeof(*sockaddr)) < 0) {
        throw std::runtime_error("failed to bind on socket");
    }
    
    if (listen(socket_fd, queue_size) < 0) {
        throw std::runtime_error("failed to listen on socket");
    }
    
    return socket_fd;
}

// registers a dest client to send outbound traffic to
int Proxy::addDestClient() {
   while(1) {
        int fd = accept(egress_socket, NULL, NULL);
        if (fd >= 0) { // TODO: check this matches the correct return vals
            std::lock_guard<std::mutex> lock(egress_mutex);
            egress_clients.push_back(fd);
            DEBUG_PRINT("egress client connected, total: %zu\n", egress_clients.size());
        } else {
            printf("accept failed due to code %d\n", fd);
        }
   }
}

// src client handler
int Proxy::addSrcClient(int fd) {
    std::byte buffer[BUFFER_SIZE];
    while(1) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0) {
            printf("recv failed due to code %ld\n", n);
            close(fd);
            break;
        } else if (n == 0) {
            DEBUG_PRINT("ingress client closed connection\n");
            close(fd);
            break;
        }

        middleware(buffer, n);
    }

    return 0;
}

// sends data to the egress clients
int Proxy::transmitter(std::byte* data, ssize_t len) {
    std::lock_guard<std::mutex> lock(egress_mutex);
    for (auto it = egress_clients.begin(); it != egress_clients.end();) {
        // poll to see if egress client still connected if not close
        pollfd pfd = { *it, POLLIN | POLLERR | POLLHUP | POLLNVAL, 0 };
        int poll_ret = poll(&pfd, 1, 0);
        if (poll_ret != 0) {
            close(*it);
            it = egress_clients.erase(it);
            DEBUG_PRINT("egress client removed\n");
            continue;
        }

        ssize_t sent = send(*it, data, len, 0);
        if (sent <= 0) { 
            close(*it);
            it = egress_clients.erase(it);
            DEBUG_PRINT("egress client removed\n");
        } else {
            DEBUG_PRINT("sent data\n");
            it++;
        }
    }

    return 0;
}

// computes 1's compliment of the data
// this is the most costly function
// tried multithreading this, but the overhead was too high
// came across something called AVX2 and may look into it later
int Proxy::validateChecksum(uint16_t real_checksum, uint16_t length_field, std::byte* data) {
    uint32_t sum = 0;
    sum += SENS_FIRST_16;
    sum += length_field;
    if (sum > MAX_UINT_16) {
        sum = (sum & MAX_UINT_16) + 1;
    }

    sum += CKSM_REPLACEMENT_VAL;
    if (sum > MAX_UINT_16) {
        sum = (sum & MAX_UINT_16) + 1;
    }

    ssize_t len = length_field + 8;

    // convert to uint16_t* for optimisation reasons
    const uint16_t* __restrict words = reinterpret_cast<const uint16_t*>(data + 8);
    int word_count = (length_field) / 2;
    for (int i = 0; i < word_count; i++) {
        sum += ntohs(words[i]);
    }

    
    if (length_field & 1) {
        uint16_t last_byte = static_cast<unsigned char>(data[len - 1]) << 8;
        sum += last_byte;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    uint16_t computed_checksum = static_cast<uint16_t>(~sum & MAX_UINT_16);
    if (computed_checksum != real_checksum) {
        return -1;
    }
    
    return 0;
}

// validates the data before calling transmitter
int Proxy::middleware(std::byte* data, ssize_t len) {
    // minimum size check
    if (len < HEADER_SIZE) {
        DEBUG_PRINT("package dropped due to being too small\n");
        return 0;
    }

    // validate magic num
    if (data[0] != std::byte{MAGIC_NUM}) {
        DEBUG_PRINT("package dropped due to invalid magic num\n");
        return 0;
    }

    // validate length
    uint16_t length_field;
    memcpy(&length_field, &data[2], sizeof(length_field));
    length_field = ntohs(length_field);
    if (length_field != (len - 8)) {
        DEBUG_PRINT("package dropped due to length mismatch\n");
        return 0;
    }

    // determine if it is sensitive
    if(static_cast<unsigned char>(data[1]) & 0x40) {
        uint16_t real_checksum;
        memcpy(&real_checksum, &data[4], sizeof(real_checksum));
        real_checksum = ntohs(real_checksum);
        int result = validateChecksum(real_checksum, length_field, data);
        if (result < 0) {
            DEBUG_PRINT("package dropped due to mismatched checksum\n");
            return 0;
        }
    }

    transmitter(data, len);
    return 0;
}

// starts the proxy. opens sockets and spawns ingress and egress threads.
int Proxy::startProxy() {
    ingress_socket = createSocket(ingress_port, 10, &ingress_sockaddr);
    printf("Proxy listening on 127.0.0.1:%d\n", ingress_port);
    egress_socket = createSocket(egress_port, MAX_EGRESS, &egress_sockaddr);
    printf("Proxy listening on 127.0.0.1:%d\n", egress_port);

    signal(SIGPIPE, SIG_IGN); // prevents silent crashes
    DEBUG_PRINT("creating egress thread...\n");
    std::thread egress_thread(&Proxy::addDestClient, this);
    egress_thread.detach();

    DEBUG_PRINT("waiting for ingress client...\n");
    while (1)
    {
        int fd = accept(ingress_socket, NULL, NULL);
        if (fd >=0) {
            DEBUG_PRINT("ingress client connected\n");
            std::thread src_thread(&Proxy::addSrcClient, this, fd);
            src_thread.detach();
        } else {
            printf("accept failed with code %d\n", fd);
        }
    }
    
    return 0;
}

Proxy::~Proxy() {
    close(egress_socket);
    close(ingress_socket);
}