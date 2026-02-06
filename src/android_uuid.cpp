#include "android_uuid.h"
#include <random>
#include <cstdio>

void uuid_generate_random(uuid_t& out) {
    std::random_device rd;
    for (int i = 0; i < 16; i++)
        out.bytes[i] = static_cast<uint8_t>(rd());
}

void uuid_unparse_lower(const uuid_t& uu, char* out) {
    std::sprintf(
        out,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uu.bytes[0], uu.bytes[1], uu.bytes[2], uu.bytes[3],
        uu.bytes[4], uu.bytes[5],
        uu.bytes[6], uu.bytes[7],
        uu.bytes[8], uu.bytes[9],
        uu.bytes[10], uu.bytes[11], uu.bytes[12], uu.bytes[13], uu.bytes[14], uu.bytes[15]
    );
}
