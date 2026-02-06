#pragma once
#include <stdint.h>

typedef struct {
    uint8_t bytes[16];
} uuid_t;

void uuid_generate_random(uuid_t& out);
void uuid_unparse_lower(const uuid_t& uu, char* out);
