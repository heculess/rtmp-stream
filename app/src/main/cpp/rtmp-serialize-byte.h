
#pragma once

#include "util/serializer.h"
#include "util/array-serializer.h"
#include <stdint.h>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif


class SerializeByte{
public:
	SerializeByte();
	virtual  ~SerializeByte();

	void write_uint8(uint8_t u8);
    void write_uint16(uint16_t u16);
    void write_uint24(uint32_t u24);
    void write_uint32(uint32_t u32);
    void write_uint64(uint64_t u64);

    size_t write(const void *data, size_t size);
    int64_t get_pos();

    std::vector<uint8_t> GetDataByte();

private:
	array_output_data data;
	serializer s;
};

#ifdef __cplusplus
}
#endif
