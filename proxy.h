#pragma once

class Proxy {
    public:
        Proxy(int ingress_port, int egress_port);
        int startProxy();
        int stopProxy();
        ~Proxy();
    private:
        int egress_socket;
        int ingress_socket;
};