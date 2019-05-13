
#include <string>

#include "util/array-serializer.h"
#include "util/darray.h"
#include "util/dstr.h"

#include "rtmp-helpers.h"
#include "rtmp-flv-packager.h"
#include "rtmp-serialize-byte.h"

std::vector<uint8_t> FLVPackager::flv_meta_data(bool write_header)
{
    std::vector<uint8_t> meta_data = build_flv_meta_data();
    int data_size = meta_data.size();
    if (data_size == 0)
        return std::vector<uint8_t>();

    SerializeByte serialize_byte;
    if (write_header) {
        serialize_byte.write("FLV", 3);
        serialize_byte.write_uint8(1);
        serialize_byte.write_uint8(5);
        serialize_byte.write_uint32(9);
        serialize_byte.write_uint32(0);
    }

    uint32_t start_pos = serialize_byte.get_pos();

    serialize_byte.write_uint8(RTMP_PACKET_TYPE_INFO);

    serialize_byte.write_uint24(data_size);
    serialize_byte.write_uint32(0);
    serialize_byte.write_uint24(0);

    serialize_byte.write(&meta_data[0], data_size);

    serialize_byte.write_uint32((uint32_t)serialize_byte.get_pos() - start_pos - 1);

    return serialize_byte.GetDataByte();
}

std::vector<uint8_t> FLVPackager::build_flv_meta_data()
{
    char buf[4096];
    char *enc = buf;
    char *end = enc+sizeof(buf);
    struct dstr encoder_name = {0};

    enc_str(&enc, end, "onMetaData");

    *enc++ = AMF_ECMA_ARRAY;
    enc    = AMF_EncodeInt32(enc, end,  20);

    for(PropertyMap::iterator it = properties.begin();it!=properties.end();it++){
        enc_num_val(&enc, end, it->first.c_str(), it->second);
    }

    dstr_printf(&encoder_name, "%s ( version %d.%d.%d )", "rtmp-output module",
                MAJOR_VER, MINOR_VER, PATCH_VER);

    enc_str_val(&enc, end, "encoder", encoder_name.array);
    dstr_free(&encoder_name);

    *enc++  = 0;
    *enc++  = 0;
    *enc++  = AMF_OBJECT_END;

    std::vector<uint8_t> buffer(enc-buf,0);

    if(enc-buf > 0)
        memcpy(&buffer[0],buf,buffer.size());

    return buffer;
}

void FLVPackager::setProperty(std::string name,double value)
{
    properties.insert(std::make_pair(name,value));
}

void FLVPackager::flv_video(SerializeByte &s, int32_t dts_offset,
                      encoder_packet &packet, bool is_header)
{
    int64_t offset  = packet.pts - packet.dts;
    int32_t time_ms = packet.get_ms_time(packet.dts) - dts_offset;

    size_t pk_size = packet.data.size();
    if (pk_size==0)
        return;

    s.write_uint8(RTMP_PACKET_TYPE_VIDEO);

    s.write_uint24((uint32_t)pk_size + 5);
    s.write_uint24(time_ms);
    s.write_uint8((time_ms >> 24) & 0x7F);
    s.write_uint24(0);

    /* these are the 5 extra bytes mentioned above */
    s.write_uint8(packet.keyframe ? 0x17 : 0x27);
    s.write_uint8(is_header ? 0 : 1);
    s.write_uint24(packet.get_ms_time(offset));
    s.write(&packet.data[0], pk_size);

    /* write tag size (starting byte doesn't count) */
    s.write_uint32((uint32_t)s.get_pos() - 1);
}

void FLVPackager::flv_audio(SerializeByte &s, int32_t dts_offset,
                      encoder_packet &packet, bool is_header)
{
    int32_t time_ms = packet.get_ms_time(packet.dts) - dts_offset;

    size_t pk_size = packet.data.size();
    if (pk_size==0)
        return;

    s.write_uint8(RTMP_PACKET_TYPE_AUDIO);

    s.write_uint24((uint32_t)pk_size + 2);
    s.write_uint24(time_ms);
    s.write_uint8((time_ms >> 24) & 0x7F);
    s.write_uint24(0);

    /* these are the two extra bytes mentioned above */
    s.write_uint8(0xaf);
    s.write_uint8(is_header ? 0 : 1);
    s.write(&packet.data[0], pk_size);

    /* write tag size (starting byte doesn't count) */
    s.write_uint32((uint32_t)s.get_pos() - 1);
}

std::vector<uint8_t> FLVPackager::flv_packet_mux(encoder_packet &packet, int32_t dts_offset,
                    bool is_header)
{
    SerializeByte serialize_byte;

    if (packet.type == OBS_ENCODER_VIDEO)
        flv_video(serialize_byte, dts_offset, packet, is_header);
    else
        flv_audio(serialize_byte, dts_offset, packet, is_header);

    return serialize_byte.GetDataByte();
}
