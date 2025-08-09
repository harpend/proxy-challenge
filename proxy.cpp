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
#include <sys/socket.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>   
#include <arpa/inet.h> 

#define BUFFER_SIZE 4096
#define MAX_EGRESS 25

class Proxy {
    public:
        Proxy(int ingress_port, int egress_port);
        int startProxy();
        int stopProxy();
        ~Proxy();
    private:
        int addDestClient();
        int egress_socket; // multiple clients
        int ingress_socket; // only one client
        int ingress_port;
        int egress_port;
        sockaddr_in ingress_sockaddr;
        sockaddr_in egress_sockaddr;
};

Proxy::Proxy(int ingress_port, int egress_port) : ingress_port(ingress_port), egress_port(egress_port) {}

int Proxy::startProxy() {
    
    ingress_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ingress_socket < 0) {
        throw std::runtime_error("ingress socket failed");
    }
    
    // listen on ingress port for 1 source client
    ingress_sockaddr = {};
    ingress_sockaddr.sin_family = AF_INET;
    ingress_sockaddr.sin_port = htons(ingress_port); 
    if (bind(ingress_socket, (struct sockaddr*)&ingress_sockaddr, sizeof(ingress_sockaddr)) < 0) {
        throw std::runtime_error("failed to bind on ingress socket");
    }
    
    if (listen(ingress_socket, 1) < 0) {
        throw std::runtime_error("failed to listen on ingress socket");
    }
    
    printf("Proxy listening on 127.0.0.1:%d", ingress_port);
    
    egress_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (egress_socket < 0) {
        throw std::runtime_error("egress socket failed");
    }
    
    // accept connections for dest clients
    egress_sockaddr = {};
    egress_sockaddr.sin_family = AF_INET;
    egress_sockaddr.sin_port = htons(ingress_port); 
    if (bind(egress_socket, (struct sockaddr*)&egress_sockaddr, sizeof(egress_sockaddr)) < 0) {
        throw std::runtime_error("failed to bind on ingress socket");
    }

    if (listen(ingress_socket, 1) < 0) {
        throw std::runtime_error("failed to listen on egress socket");
    }
    
    printf("Proxy listening on 127.0.0.1:%d", egress_port);
    fd_set readfds;
    char buffer[BUFFER_SIZE];
    int outbounds[MAX_EGRESS];
    int egress_connections = 0;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    // handle connection requests
    while (1) { // TODO: move to separate function
        FD_ZERO(&readfds);
        FD_SET(ingress_socket, &readfds);
        FD_SET(egress_socket, &readfds);
        int maxfd = (ingress_socket > egress_socket ? ingress_socket : egress_socket); 

        if (ingress_socket != -1) {
            FD_SET(ingress_socket, &readfds);
            if (ingress_socket > maxfd) maxfd = ingress_socket;
        }

        for (int i = 0; i < egress_connections; i++) {
            FD_SET(outbounds[i], &readfds);
            if (outbounds[i] > maxfd) maxfd = outbounds[i];
        }

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            throw std::runtime_error("failed accepting connection");
        }

        if (FD_ISSET(ingress_socket, &readfds)) {
            int new_fd = accept(ingress_socket, (struct sockaddr*)&ingress_sockaddr, &client_len);
            if (new_fd < 0) {
                throw std::runtime_error("failed to accept ingress client");
            }

            if (ingress_socket == -1) {
                ingress_socket = new_fd;
            } else {
                printf("more than 1 simultatneous ingress connections attempted.\n");
                close(new_fd);
            }
        }

        if (FD_ISSET(egress_socket, &readfds)) {
            int new_fd = accept(egress_socket, (struct sockaddr*)&client_addr, &client_len);
            if (new_fd < 0) {
                throw std::runtime_error("failed to accept egress client");
            }

            if (egress_connections < MAX_EGRESS) {
                outbounds[egress_connections++] = new_fd;
            } else {
                printf("more than %d simultatneous egress connections attempted.\n", MAX_EGRESS);
                close(new_fd);
            }
        }

        if (ingress_socket != -1 && FD_ISSET(ingress_socket, &readfds)) {
            ssize_t n = read(ingress_socket, buffer, sizeof(buffer));
            printf("data received");
            if (n <= 0) {
                close(ingress_socket);
                ingress_socket = -1;
            } else {
                for (int i = 0; i < egress_connections; i++) {
                    if (write(outbounds[i], buffer, n) != n) {
                        throw std::runtime_error("failed to send to outbound");
                    }
                }
            }
        }

    }

    close(ingress_socket);
    close(egress_socket);
    return 0;
}

Proxy::~Proxy() {
    close(egress_socket);
    close(ingress_socket);
}