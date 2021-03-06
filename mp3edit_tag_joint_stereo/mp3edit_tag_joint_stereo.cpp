// mp3analyzer.cpp :
//
// Copyright (c) 2010, Reiji Tokuda
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
// 
// - Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "stdafx.h"
#include "stdint.h"
#include "memory.h"

///////////////////////////////////////

#define MP3_SAMPLE_PER_FRAME 1152
#define MP3_BYTE_ATTR_PER_FRAME 144

/**
 * function
 *
 */
typedef int (*id3_force_joint_stereo)(FILE *src_fp, FILE *dst_fp);
typedef int (*id3_analyzation)(FILE *fp,
                            uint32_t *num_frame,
                            uint32_t *sample_rate,
                            uint8_t *channel,
                            uint8_t *version,
                            uint8_t *layer);
typedef int (*id3_skip_frame)(FILE *fp, uint32_t *frame);

typedef struct mp3demuxer_context_tag {
    char *src_filename;
    char *dst_filename;

    uint32_t sample_rate;
    uint8_t sample_bit;
    uint8_t channel;

    id3_force_joint_stereo force_js;
    id3_analyzation analyze;
    id3_skip_frame skip_frame;
} mp3demuxer_context;

///////////////////////////////////////

/**
 * ID3用関数
 *
 *
 */
static int
id3_analyzation_v1(FILE *fp,
                    uint32_t *num_frame,
                    uint32_t *sample_rate,
                    uint8_t *channel,
                    uint8_t *version,
                    uint8_t *layer);

static int
id3_analyzation_v1_internal(FILE *fp,
                    uint32_t begin_pos,
                    uint32_t *num_frame,
                    uint32_t *sample_rate,
                    uint8_t *channel,
                    uint8_t *version,
                    uint8_t *layer);
static int
id3_analyzation_v2(FILE *fp,
                    uint32_t *num_frame,
                    uint32_t *sample_rate,
                    uint8_t *channel,
                    uint8_t *version,
                    uint8_t *layer);

static int
id3_skip_frame_v1(FILE *fp, uint32_t *frame);

static int
id3_skip_frame_v1_internal(FILE *fp, uint32_t begin_pos, uint32_t *frame);

static int
id3_skip_frame_v2(FILE *fp, uint32_t *frame);

static int
id3_force_js_v1(FILE *src_fp, FILE *dst_fp);
static int
id3_force_js_v1_internal(FILE *src_fp, FILE *dst_fp, uint32_t begin_pos);
static int
id3_force_js_v2(FILE *src_fp, FILE *dst_fp);

/**
 * 
 *
 *
 */

typedef struct mp3_frame_header_tag {
    uint8_t version;
    uint8_t layer;
    uint8_t protection_bit;
    uint8_t bitrate_index;
    uint8_t sampling_frequency_index;
    uint8_t padding_bit;
    uint8_t private_bit;
    uint8_t channel_mode;
    uint8_t mode_extension;
    uint8_t copyright;
    uint8_t original;
    uint8_t emphasis;
} mp3_frame_header;

typedef struct mp3_id3v1_tag {
}mp3_id3v1;

typedef struct mp3_id3v2_tag {
}mp3_id3v2;

typedef enum mediatypes
{
    Unknown = 0,

    MPEG1A = 1,
    MPEG2A = 2,
    MPEG3A = 3,// MPEG Layer-3 Audio

} mediatypes_t;

//[version][layer]
static uint32_t mediatype_table[][4] =
{
    {Unknown, MPEG2A, MPEG2A, MPEG2A},
    {Unknown, Unknown, Unknown, Unknown},
    {Unknown, MPEG2A, MPEG2A, MPEG3A},
    {Unknown, MPEG1A, MPEG1A, MPEG1A}
};

//[version][layer][value]
static uint32_t bitrate_table[][4][16] = 
{
    {//mpeg2.5
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},//reserved
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},//mpeg2.5
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},//mpeg2.5
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0} //mpeg2.5
    },
    {//reserved
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},//reserved
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},//reserved
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},//reserved
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} //reserved
    },
    {//mpeg2
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},//reserved
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},//mpeg2 L3
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},//mpeg2 L2
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0}//mpeg2 L1
    },
    {//mpeg1
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},//reserved
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},//mpeg1 L3
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},//mpeg1 L2
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0}//mpeg1 L1
    }
};

//[version][index]
static uint32_t sampling_rate_table[][4] = 
{
    {11025, 12000, 8000, 0},//mpeg2.5
    {0, 0, 0, 0},//reserved
    {22050, 24000, 16000, 0},//mpeg2
    {44100, 48000, 32000, 0} //mpeg1
};

//
static uint32_t channel_table[] = 
{
    2, 2, 2, 1
};


static void
dump_mp3header(uint32_t frame_num, mp3_frame_header *header);

///////////////////////////////////////



int main(int argc, char* argv[])
{
    if(argc < 3) {
        fprintf(stderr, "*error* : comnadline argument must be lager than 1\n");
        return -1;
    }


    //
    mp3demuxer_context mp3demuxer;
    FILE *src_fp;
    FILE *dst_fp;
    uint8_t mp3_header[4];
    size_t read_size;

    //init
    memset(&mp3demuxer, 0x00, sizeof(mp3demuxer_context));

    mp3demuxer.src_filename = argv[1];
    mp3demuxer.dst_filename = argv[2];

    //check header
    src_fp = fopen(mp3demuxer.src_filename, "rb");//open
    if(!src_fp) {
        fprintf(stderr, "*error* : file open failed : at %s\n", mp3demuxer.src_filename);
    fclose(src_fp);//close
        return -1;
    }
    read_size = fread(mp3_header, 1, 4, src_fp);
    fclose(src_fp);//close

    if(read_size != 4) {
        fprintf(stderr, "*error* : invalid file size\n");
        return -1;
    }

    if(mp3_header[0] == 0xff ||
        (mp3_header[1] & 0xe0) == 0xe0) {//MP3Header
            mp3demuxer.force_js = id3_force_js_v1;
            mp3demuxer.analyze = id3_analyzation_v1;
            mp3demuxer.skip_frame = id3_skip_frame_v1;
            printf("mp3v1\n");
    }
    else if (mp3_header[0] == 0x49 &&
                mp3_header[1] == 0x44 &&
                mp3_header[2] == 0x33) {//ID3Header
            mp3demuxer.force_js = id3_force_js_v2;
            mp3demuxer.analyze = id3_analyzation_v2;
            mp3demuxer.skip_frame = id3_skip_frame_v2;
            printf("mp3v2\n");
    }
    else {
        fprintf(stderr, "*error* : invalid header\n");
        return -1;
    }

    
    //
    src_fp = fopen(mp3demuxer.src_filename, "rb");//open
    if(!src_fp) {
        fprintf(stderr, "*error* : file open for analyzation failed : at %s\n", mp3demuxer.src_filename);
        return -1;
    }    
    //
    dst_fp = fopen(mp3demuxer.dst_filename, "wb");//open
    if(!dst_fp) {
        fprintf(stderr, "*error* : file open for analyzation failed : at %s\n", mp3demuxer.dst_filename);
        return -1;
    }

    if(mp3demuxer.force_js(src_fp, dst_fp)) {
        fprintf(stderr, "*error* : analyzation failed\n");
        fclose(src_fp);//close
        fclose(dst_fp);//close
        return -1;
    }
    fclose(src_fp);//close
    fclose(dst_fp);//close


    return 0;
}

///////////////////////////////////////////////////////////////////
static int
id3_analyzation_v1(FILE *fp,
                    uint32_t *num_frame,
                    uint32_t *sample_rate,
                    uint8_t *channel,
                    uint8_t *version,
                    uint8_t *layer)
{
    return id3_analyzation_v1_internal(fp, 0, num_frame, sample_rate, channel, version, layer);
}
//半端フレームの処理なし
static int
id3_analyzation_v1_internal(FILE *fp,
                    uint32_t begin_pos,
                    uint32_t *num_frame,
                    uint32_t *sample_rate,
                    uint8_t *channel,
                    uint8_t *version,
                    uint8_t *layer)
{
    mp3_frame_header header;
    uint8_t data[4];
    size_t read_size;
    size_t frame_count = 0;

    size_t frame_size = 0;
    size_t data_end;

    uint32_t sr, br;

    uint8_t tag_exist = 0;

    if(fseek(fp, 0, SEEK_END))//end
        return -1;

    data_end = ftell(fp);

    //TAG
    if(data_end > 128) {
        fseek(fp, -128, SEEK_END);
        fread(data, 1, 4, fp);
        if(0 == memcmp(data, "TAG", 3))
            tag_exist = 1;
    }
    if(tag_exist)
        data_end -= 128;

    if(fseek(fp, begin_pos, SEEK_SET))//start
        return -1;

    while (1) {

        if(data_end <= ftell(fp))
            break;//end
    
        read_size = fread(data, 1, 4, fp);

        if(read_size < 4) {
            return -1;//error
        }


#if 1
        if(data[0] != 0xff ||
            data[1] & 0xe0 != 0xe0)
                return -2;//error

#else
        if(data[0] != 0xff ||
            data[1] & 0xe0 != 0xe0) {

                while(true) {
                    read_size = fread(data, 1, 4, fp);

                    if(read_size < 4) {
                        return RME_IO_ERROR;//error
                    }

                    if(data[0] == 0xff &&
                        data[1] == 0xff &&
                        data[2] == 0xff &&
                        data[3] == 0xff)
                        break;
                }
                continue;
        }
#endif

        header.version = (data[1] & 0x18) >> 3;
        header.layer = (data[1] & 0x06) >> 1;
        header.protection_bit = (data[1] & 0x01);
        header.bitrate_index = (data[2] & 0xF0) >> 4;
        header.sampling_frequency_index = (data[2] & 0x06) >> 2;
        header.padding_bit = (data[2] & 0x02) >> 1;
        header.private_bit = (data[2] & 0x01);
        header.channel_mode = (data[3] & 0xc0) >> 6;
        header.mode_extension = (data[3] & 0x30) >> 4;
        header.copyright = (data[3] & 0x08) >> 3;
        header.original = (data[3] & 0x04) >> 2;
        header.emphasis = (data[3] & 0x03);

        dump_mp3header(frame_count, &header);//dump

        frame_count++;

        sr = sampling_rate_table[header.version][header.sampling_frequency_index];//sampling rate
        br = bitrate_table[header.version][header.layer][header.bitrate_index] * 1000;//bitrate

        frame_size = (MP3_BYTE_ATTR_PER_FRAME * br / sr) + header.padding_bit;

        if(fseek(fp, frame_size - 4, SEEK_CUR))
            return -1;
    }

    *num_frame = frame_count;
    *sample_rate = sampling_rate_table[header.version][header.sampling_frequency_index];
    *channel = channel_table[header.channel_mode];
    *version = header.version;
    *layer = header.layer;

    return 0;
}

static int
id3_analyzation_v2(FILE *fp,
                    uint32_t *num_frame,
                    uint32_t *sample_rate,
                    uint8_t *channel,
                    uint8_t *version,
                    uint8_t *layer)
{
    uint8_t data[10];
    uint32_t read_size;
    uint32_t id3v2_length;

    read_size = fread(data, 1, 10, fp);
    if(read_size != 10)
        return -1;
    
    if(data[0] != 0x49 ||
        data[1] != 0x44 ||
        data[2] != 0x33)
        return -2;

    id3v2_length = 0;
    id3v2_length = ((data[6] & 0x7F) << 21) |
                    ((data[7] & 0x7F) << 14) |
                    ((data[8] & 0x7F) << 7) |
                    ((data[9] & 0x7F) << 0);

    if(fseek(fp, id3v2_length, SEEK_CUR))
        return -1;

    //search

    return id3_analyzation_v1_internal(fp, ftell(fp), num_frame, sample_rate, channel, version, layer);
}

static int
id3_skip_frame_v1(FILE *fp, uint32_t *frame)
{
    int result;
    uint32_t skip_first = 1;
    result = id3_skip_frame_v1_internal(fp, 0, &skip_first);
    if(0 != result)//skip first frame
        return result;
    if(1 != skip_first)
        return -2;

    return id3_skip_frame_v1_internal(fp, ftell(fp), frame);
}

static int
id3_skip_frame_v1_internal(FILE *fp, uint32_t begin_pos, uint32_t *frame)
{
    mp3_frame_header header;
    uint8_t data[4];
    size_t read_size;
    size_t frame_count = 0;

    size_t frame_size = 0;

    uint32_t sr, br;

    uint8_t tag_exist = 0;

    if(fseek(fp, begin_pos, SEEK_SET))//start
        return -1;

    while (frame_count < *frame) {
    
        read_size = fread(data, 1, 4, fp);

        if(read_size < 4) {
            *frame = frame_count;
            if(feof(fp))
                return 1;
            else
                return -1;//error
        }
        
#if 1
        if(data[0] != 0xff ||
            data[1] & 0xe0 != 0xe0)
                return -2;//error

#else
        if(data[0] != 0xff ||
            data[1] & 0xe0 != 0xe0) {

                while(true) {
                    read_size = fread(data, 1, 4, fp);

                    if(read_size < 4) {
                        return RME_IO_ERROR;//error
                    }

                    if(data[0] == 0xff &&
                        data[1] == 0xff &&
                        data[2] == 0xff &&
                        data[3] == 0xff)
                        break;
                }
                continue;
        }
#endif

        header.version = (data[1] & 0x18) >> 3;
        header.layer = (data[1] & 0x06) >> 1;
        header.protection_bit = (data[1] & 0x01);
        header.bitrate_index = (data[2] & 0xF0) >> 4;
        header.sampling_frequency_index = (data[2] & 0x06) >> 2;
        header.padding_bit = (data[2] & 0x02) >> 1;
        header.private_bit = (data[2] & 0x01);
        header.channel_mode = (data[3] & 0xc0) >> 6;
        header.mode_extension = (data[3] & 0x30) >> 4;
        header.copyright = (data[3] & 0x08) >> 3;
        header.original = (data[3] & 0x04) >> 2;
        header.emphasis = (data[3] & 0x03);

        frame_count++;

        sr = sampling_rate_table[header.version][header.sampling_frequency_index];//sampling rate
        br = bitrate_table[header.version][header.layer][header.bitrate_index] * 1000;//bitrate

        frame_size = (MP3_BYTE_ATTR_PER_FRAME * br / sr) + header.padding_bit;

        if(fseek(fp, frame_size - 4, SEEK_CUR))
            return -1;
    }


    return 0;
}
static int
id3_skip_frame_v2(FILE *fp, uint32_t *frame)
{
    uint8_t data[10];
    uint32_t read_size;
    uint32_t id3v2_length;

    uint32_t skip_first = 1;

    if(fseek(fp, 0, SEEK_SET))
        return -1;

    read_size = fread(data, 1, 10, fp);
    if(read_size != 10)
        return -1;
    
    if(data[0] != 0x49 ||
        data[1] != 0x44 ||
        data[2] != 0x33)
        return -1;

    id3v2_length = 0;
    id3v2_length = ((data[6] & 0x7F) << 21) |
                    ((data[7] & 0x7F) << 14) |
                    ((data[8] & 0x7F) << 7) |
                    ((data[9] & 0x7F) << 0);

    if(fseek(fp, id3v2_length, SEEK_CUR))
        return -1;

    int result;
    result = id3_skip_frame_v1_internal(fp, ftell(fp), &skip_first);
    if(0 != result)//skip first frame
        return result;
    if(1 != skip_first)
        return -2;

    return id3_skip_frame_v1_internal(fp, ftell(fp), frame);
}

////////////////////////////////////////////////


static char* version_string[] =
{
    "LSF extention",
    "HSF"
};

static char* layer_string[] =
{
    "Reserved",
    "Layer3",
    "Layer2",
    "Layer1",
};


static char* protection_bit_string[] =
{
    "CRC",
    "NON",
};

static char* padding_bit_string[] =
{
    "0",
    "1"
};

static char* channel_string[] =
{
    "stereo",
    "joint stereo",
    "dual channel",
    "single channel",
};

static char* mode_extention_string[] =
{
    "subband 4+",
    "subband 8+",
    "subband 12+",
    "subband 16+",
};

static char* copyringht_string[] =
{
    "copyright disabled",
    "copyright enabled",
};

static char* original_string[] =
{
    "copied",
    "original",
};

static char* enphasisl_string[] =
{
    "NON",
    "50/15 us",
    "Reserved",
    "CCITT J.17",
};

static void
dump_mp3header(uint32_t frame_num, mp3_frame_header *header) {

    if(!header)
        return;
    
    uint32_t sr = sampling_rate_table[header->version][header->sampling_frequency_index];//sampling rate
    uint32_t br = bitrate_table[header->version][header->layer][header->bitrate_index] * 1000;//bitrate

    uint32_t frame_size = (MP3_BYTE_ATTR_PER_FRAME * br / sr) + header->padding_bit;
    
    //dump
    printf(
        "------------------------------------------------------------------------------\n"
        "Frame : %08d   frame size : %d   version : %s   layer : %s\n"
        "sampling rate  : %d   bit rate : %d   channel : %s\n"
        "protection bit : %s   padding bit : %s   private bit : %d\n"
        "extention mode : %s   copyright : %s   original : %s   enphasis : %s\n"
        "------------------------------------------------------------------------------\n"
        ,
        frame_num, frame_size,
        version_string[header->version & 0x01],
        layer_string[header->layer],

        sr, br, channel_string[header->channel_mode],

        protection_bit_string[header->protection_bit],
        padding_bit_string[header->padding_bit],
        header->private_bit,
        
        mode_extention_string[header->mode_extension],
        copyringht_string[header->copyright],
        original_string[header->original],
        enphasisl_string[header->emphasis]);
        
        
        
}


///////////////////////////////////////////////////////////////////
static int
id3_force_js_v1(FILE *src_fp, FILE *dst_fp)
{
    return id3_force_js_v1_internal(src_fp, dst_fp, 0);
}
//半端フレームの処理なし
static int
id3_force_js_v1_internal(FILE *src_fp, FILE *dst_fp, uint32_t begin_pos)
{
    mp3_frame_header header;
    uint8_t data[4];
    size_t read_size;
    size_t wrote_size;
    size_t frame_count = 0;

    size_t frame_size = 0;
    size_t data_end;

    uint32_t sr, br;

    uint8_t tag_exist = 0;

    uint8_t translate_buffer[5 * 1024];
    uint8_t transrate_tail_buffer[128];

    if(fseek(src_fp, 0, SEEK_END))//end
        return -1;

    data_end = ftell(src_fp);

    //TAG
    if(data_end > 128) {
        fseek(src_fp, -128, SEEK_END);
        fread(transrate_tail_buffer, 1, 128, src_fp);
        if(0 == memcmp(transrate_tail_buffer, "TAG", 3))
            tag_exist = 1;
    }
    if(tag_exist)
        data_end -= 128;

    // HEADER
    //if(fseek(fp, begin_pos, SEEK_SET))//start
    //    return -1;
    if(fseek(src_fp, 0, SEEK_SET))//seek
        return -1;
    read_size = fread(translate_buffer, 1, begin_pos, src_fp);//read
    if(read_size != begin_pos)
        return -1;
    wrote_size = fwrite(translate_buffer, 1, read_size, dst_fp);//write
    if(wrote_size != read_size)
        return -1;

    while (1) {

        if(data_end <= ftell(src_fp))
            break;//end
    
        read_size = fread(data, 1, 4, src_fp);

        if(read_size < 4) {
            return -1;//error
        }


#if 1
        if(data[0] != 0xff ||
            data[1] & 0xe0 != 0xe0)
                return -2;//error

#else
        if(data[0] != 0xff ||
            data[1] & 0xe0 != 0xe0) {

                while(true) {
                    read_size = fread(data, 1, 4, fp);

                    if(read_size < 4) {
                        return RME_IO_ERROR;//error
                    }

                    if(data[0] == 0xff &&
                        data[1] == 0xff &&
                        data[2] == 0xff &&
                        data[3] == 0xff)
                        break;
                }
                continue;
        }
#endif

        header.version = (data[1] & 0x18) >> 3;
        header.layer = (data[1] & 0x06) >> 1;
        header.protection_bit = (data[1] & 0x01);
        header.bitrate_index = (data[2] & 0xF0) >> 4;
        header.sampling_frequency_index = (data[2] & 0x06) >> 2;
        header.padding_bit = (data[2] & 0x02) >> 1;
        header.private_bit = (data[2] & 0x01);
        header.channel_mode = (data[3] & 0xc0) >> 6;
        header.mode_extension = (data[3] & 0x30) >> 4;
        header.copyright = (data[3] & 0x08) >> 3;
        header.original = (data[3] & 0x04) >> 2;
        header.emphasis = (data[3] & 0x03);

        //dump_mp3header(frame_count, &header);//dump

        frame_count++;

        sr = sampling_rate_table[header.version][header.sampling_frequency_index];//sampling rate
        br = bitrate_table[header.version][header.layer][header.bitrate_index] * 1000;//bitrate

        frame_size = (MP3_BYTE_ATTR_PER_FRAME * br / sr) + header.padding_bit;

        //data manipuration //******
        data[3] &= 0x3F;
        data[3] |= 0x40;

        //if(fseek(fp, frame_size - 4, SEEK_CUR))
        //    return -1;
        read_size = fread(translate_buffer, 1, frame_size - 4, src_fp);//read
        if(read_size != frame_size - 4)
            return -1;
        wrote_size = fwrite(data, 1, 4, dst_fp);//write
        if(wrote_size != 4)
            return -1;
        wrote_size = fwrite(translate_buffer, 1, read_size, dst_fp);//write
        if(wrote_size != read_size)
            return -1;
    }

    //write mp3tag
    wrote_size = fwrite(transrate_tail_buffer, 1, 128, dst_fp);//write
    if(wrote_size != 128)
        return -1;

    return 0;
}

static int
id3_force_js_v2(FILE *src_fp, FILE *dst_fp)
{
    uint8_t data[10];
    uint32_t read_size;
    uint32_t id3v2_length;

    read_size = fread(data, 1, 10, src_fp);
    if(read_size != 10)
        return -1;
    
    if(data[0] != 0x49 ||
        data[1] != 0x44 ||
        data[2] != 0x33)
        return -2;

    id3v2_length = 0;
    id3v2_length = ((data[6] & 0x7F) << 21) |
                    ((data[7] & 0x7F) << 14) |
                    ((data[8] & 0x7F) << 7) |
                    ((data[9] & 0x7F) << 0);

    if(fseek(src_fp, id3v2_length, SEEK_CUR))
        return -1;

    //search

    return id3_force_js_v1_internal(src_fp, dst_fp, ftell(src_fp));
}
