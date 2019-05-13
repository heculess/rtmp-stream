

#pragma once

#include <map>
#include <string>
#include <vector>
#include "rtmp-defs.h"
#include "util/serializer.h"


#ifdef __cplusplus
extern "C" {
#endif

class SerializeByte;

class FLVPackager {
public:
    std::vector<uint8_t> flv_meta_data(bool write_header);

    static std::vector<uint8_t> flv_packet_mux(encoder_packet &packet, int32_t dts_offset,
                        bool is_header);

    void setProperty(std::string name, double value);

private:

    typedef std::map<std::string, double> PropertyMap;
    PropertyMap properties;

    std::vector<uint8_t> build_flv_meta_data();

    static void flv_video(SerializeByte &s, int32_t dts_offset,
                                       encoder_packet &packet, bool is_header);
    static void flv_audio(SerializeByte &s, int32_t dts_offset,
                                       encoder_packet &packet, bool is_header);
};

#ifdef __cplusplus
}
#endif

