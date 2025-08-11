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

#define BUFFER_SIZE 65543 // 0xFFFF + header (8 bytes)
#define MAX_EGRESS 100
#define MAX_UINT_16 0xFFFF
#define SENS_FIRST_16 0xCC40
#define CKSM_REPLACEMENT_VAL 0xCCCC
#define CKSM_THREAD_LWR_BOUND 1000
#define CKSM_THREAD_MID_BOUND 2000
#define MAGIC_NUM 0xCC
#define HEADER_SIZE 8

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
        int validateChecksum(uint16_t real_checksum, uint16_t length_field, std::byte* data);
        int multiThreadValidateChecksum(const uint16_t* words, uint16_t length_field);
        int threadValidateChecksum(const uint16_t* words, int start, int end, uint32_t& sub_sum);
        int egress_socket; // multiple clients
        int ingress_socket; // only one client
        int ingress_port;
        int egress_port;
        sockaddr_in ingress_sockaddr;
        sockaddr_in egress_sockaddr;
        std::vector<int> egress_clients;
        std::mutex egress_mutex;
};