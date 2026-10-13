#ifndef COMMON_H
#define COMMON_H

struct patch
{
    uint64_t offset;
    const char data[0x100];
    int size;
};


#endif // COMMON_H