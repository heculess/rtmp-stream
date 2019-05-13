
#include "rtmp-serialize-byte.h"

SerializeByte::SerializeByte()
{
    array_output_serializer_init(&s, &data);
}

SerializeByte::~SerializeByte()
{
    array_output_serializer_free(&data);
}

void SerializeByte::write_uint8(uint8_t u8)
{
    s_w8(&s, u8);
}

void SerializeByte::write_uint16(uint16_t u16)
{
    s_wb16(&s, u16);
}

void SerializeByte::write_uint24(uint32_t u24)
{
    s_wb24(&s, u24);
}

void SerializeByte::write_uint32(uint32_t u32)
{
    s_wb32(&s, u32);
}

void SerializeByte::write_uint64(uint64_t u64)
{
    s_wl64(&s, u64);
}

size_t SerializeByte::write(const void *data, size_t size)
{
    return s_write(&s, data, size);
}

int64_t SerializeByte::get_pos()
{
    if (s.get_pos)
        return s.get_pos(s.data);
    return -1;
}

std::vector<uint8_t> SerializeByte::GetDataByte()
{
    if(data.bytes.num > 0){
        std::vector<uint8_t> buffer(data.bytes.num,0);
        memcpy(&buffer[0],data.bytes.array,buffer.size());
        return buffer;
    }
    return std::vector<uint8_t>();
}


