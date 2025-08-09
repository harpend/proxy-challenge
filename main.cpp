#include "proxy.h"

#define SRC_CLIENT 33333
#define DST_CLIENT 44444

int main() {
    Proxy p = Proxy(SRC_CLIENT, DST_CLIENT);
    return 0;
}