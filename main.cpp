#include "proxy.h"
#include <stdexcept>
#include <iostream>

#define SRC_CLIENT 33333
#define DST_CLIENT 44444

int main() {
    Proxy p(SRC_CLIENT, DST_CLIENT);
    try {
        printf("starting proxy...\n");
        p.startProxy();
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}