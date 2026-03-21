// wasm_crc32_shim.h -- portability shim for WASM/non-x86 targets
// Force-include this before algo.h when compiling with emcc
#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Emscripten's stdio.h defines stdout as a macro, which conflicts with
// struct fields named 'stdout' in generated code
#include <stdio.h>
#undef stdout
#undef stdin
#undef stderr
#endif

#ifndef __SSE4_2__

#include <stdint.h>

// CRC32C (Castagnoli polynomial 0x1EDC6F41) - software implementation
static inline uint32_t _wasm_crc32c_byte(uint32_t crc, uint8_t val) {
    crc ^= val;
    for (int i = 0; i < 8; i++) {
        crc = (crc >> 1) ^ (0x82F63B78u * (crc & 1));
    }
    return crc;
}

static inline uint32_t _mm_crc32_u8(uint32_t prev, uint8_t val) {
    return _wasm_crc32c_byte(prev, val);
}

static inline uint32_t _mm_crc32_u16(uint32_t prev, uint16_t val) {
    prev = _wasm_crc32c_byte(prev, val & 0xff);
    prev = _wasm_crc32c_byte(prev, (val >> 8) & 0xff);
    return prev;
}

static inline uint32_t _mm_crc32_u32(uint32_t prev, uint32_t val) {
    for (int i = 0; i < 4; i++) {
        prev = _wasm_crc32c_byte(prev, val & 0xff);
        val >>= 8;
    }
    return prev;
}

static inline uint32_t _mm_crc32_u64(uint32_t prev, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        prev = _wasm_crc32c_byte(prev, val & 0xff);
        val >>= 8;
    }
    return prev;
}

// Prevent algo.inl.h from redefining _mm_crc32_* shims
#define WASM_CRC32_SHIM_DEFINED

#endif // __SSE4_2__
