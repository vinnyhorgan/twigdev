#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>

typedef struct {
    void *data;
    bool close;
    int width, height;
    void *buffer;
    int buffer_width, buffer_height;
    int dst_ox, dst_oy, dst_width, dst_height;
} SysWindow;

SysWindow* sys_open(const char* title, int width, int height);
int sys_update(SysWindow* window, void *buffer, int width, int height);

#endif
