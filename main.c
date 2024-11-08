#include <stdlib.h>
#include <stdint.h>

#include "system.h"

int main() {
    SysWindow *window = sys_open("Twig bitches", 640, 480);

    uint32_t *buffer = malloc(320 * 180 * 4);
    buffer[10 + 320 * 10] = 0xff0000ff;

    while (true) {
        int ret = sys_update(window, buffer, 320, 180);
        if (ret) { break; }
    }

    return 0;
}
