#pragma once

#include <cstddef>
#include <vector>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <thread>
#include <cstddef>

class Proxy {
    public:
        Proxy(int ingress_port, int egress_port);
        int startProxy();
        int stopProxy();
        ~Proxy();
    private:
        int addDestClient();
        int addSrcClient(int fd);
        int createSocket(int port, int queue_size, sockaddr_in* sockaddr);
        int middleware(std::byte*, ssize_t len); // use byte cpp instead
        int transmitter(std::byte*, ssize_t len);
        int egress_socket; // multiple clients
        int ingress_socket; // only one client
        int ingress_port;
        int egress_port;
        sockaddr_in ingress_sockaddr;
        sockaddr_in egress_sockaddr;
        std::vector<int> egress_clients;
        std::mutex egress_mutex;
};