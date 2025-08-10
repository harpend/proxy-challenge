// https://doc.lagout.org/programmation/unix/Unix%20Network%20Programming%20Volume%201.pdf
// https://www.youtube.com/watch?v=JRTLSxGf_6w
// NOTES TO SELF
// man bind(): bind assigns the address specified by *sockaddr to the socket
// man socket(): creates an endpoint for communication **assings the lowest number fd not currently open**
// man connect(): attempts to make a connection to the socket we have binded
// man select(): allows a program to monitor multiple file descriptors (I think this means a socket is handled as if it were a file?)
// we probably want to spawn a new thread over using select TODO: measure overhead of threads vs sockets
// man accept(): creates a new connected socket and returns a new file descriptor referring to the socket

#include "proxy.h"
#include "ctmp.h"
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

#define BUFFER_SIZE 65543 // 0xFFFF + header (8 bytes)
#define MAX_EGRESS 25

Proxy::Proxy(int ingress_port, int egress_port) : ingress_port(ingress_port), egress_port(egress_port) {}

int Proxy::createSocket(int port, int queue_size, sockaddr_in* sockaddr) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        throw std::runtime_error("ingress socket failed");
    }
    
    *sockaddr = {};
    sockaddr->sin_family = AF_INET;
    sockaddr->sin_port = htons(port);
    sockaddr->sin_addr.s_addr =  inet_addr("127.0.0.1");
    if (bind(socket_fd, (struct sockaddr*)sockaddr, sizeof(*sockaddr)) < 0) {
        throw std::runtime_error("failed to bind on socket");
    }
    
    if (listen(socket_fd, queue_size) < 0) {
        throw std::runtime_error("failed to listen on socket");
    }
    
    return socket_fd;
}

int Proxy::addDestClient() {
   while(1) {
        int fd = accept(egress_socket, NULL, NULL);
        if (fd >= 0) { // TODO: check this matches the correct return vals
            std::lock_guard<std::mutex> lock(egress_mutex);
            egress_clients.push_back(fd);
            printf("egress client connected, total: %zu\n", egress_clients.size());
        }
   }
}

int Proxy::addSrcClient(int fd) {
    std::byte buffer[BUFFER_SIZE];
    while(1) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0) {
            // error occured
            close(fd);
            throw std::runtime_error("error receiving data\n"); // remove when making it production
        } else if (n == 0) {
            // client closed connection
            close(fd);
            break;
        }

        middleware(buffer, n);
    }

    return 0;
}

int Proxy::transmitter(std::byte* data, size_t len) {
    std::lock_guard<std::mutex> lock(egress_mutex);
    for (auto it = egress_clients.begin(); it != egress_clients.end();) {
        ssize_t sent = send(*it, data, len, 0);
        if (sent <= 0) { // TODO: handle proper ret vals
            close(*it);
            it = egress_clients.erase(it);
        } else {
            ++it;
        }
    }

    return 0;
}

// validates the data before calling transmitter
int Proxy::middleware(std::byte* data, size_t len) {
    // minimum size check
    if (len < HEADER_SIZE) {
        printf("package dropped due to being too small\n");
        return 0; // maybe change the return later?
    }

    // validate magic num
    if (data[0] != std::byte{MAGIC_NUM}) {
        printf("package dropped due to invalid magic num\n");
        return 0; // maybe change the return later?
    }

    // validate length
    uint16_t length_field;
    memcpy(&length_field, &data[2], sizeof(length_field));
    length_field = ntohs(length_field);
    if (length_field != (len - 8)) {
        printf("package dropped due to length mismatch\n");
        return 0; // maybe change the return later? or could throw err
    }

    transmitter(data, len);
    return 0;
}

int Proxy::startProxy() {
    try {
        ingress_socket = createSocket(ingress_port, 1, &ingress_sockaddr);
        printf("Proxy listening on 127.0.0.1:%d\n", ingress_port);
        egress_socket = createSocket(egress_port, MAX_EGRESS, &egress_sockaddr);
        printf("Proxy listening on 127.0.0.1:%d\n", egress_port);
    } catch(const std::runtime_error& e) {
        throw e; // pointless
    }

    printf("creating egress thread...\n");
    std::thread egress_thread(&Proxy::addDestClient, this);
    egress_thread.detach();
    
    printf("waiting for ingress client...\n");
    while (1)
    {
        int fd = accept(ingress_socket, NULL, NULL);
        if (fd >=0) { // TODO: check return values
            addSrcClient(fd);
        }
    }
    
    return 0;
}

Proxy::~Proxy() {
    close(egress_socket);
    close(ingress_socket);
}