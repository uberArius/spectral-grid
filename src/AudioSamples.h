#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace sg {
// WASAPI extensible PCM stores valid bits left-aligned in its container.
// Dividing by the container's signed range consequently handles 24-in-32 too.
inline float decodeAudioSample(const uint8_t* p,unsigned bits,bool floating) {
    float result=0;
    if(floating && bits==32) std::memcpy(&result,p,4);
    else if(floating && bits==64) { double d; std::memcpy(&d,p,8); result=float(d); }
    else if(bits==8) result=(int(*p)-128)/128.0f;
    else if(bits==16) { int16_t v; std::memcpy(&v,p,2); result=v/32768.0f; }
    else if(bits==24) { int32_t v=int32_t(p[0])|(int32_t(p[1])<<8)|(int32_t(p[2])<<16); if(v&0x800000) v|=static_cast<int32_t>(0xFF000000u); result=v/8388608.0f; }
    else if(bits==32) { int32_t v; std::memcpy(&v,p,4); result=float(v/2147483648.0); }
    return std::isfinite(result)?std::clamp(result,-8.0f,8.0f):0;
}
inline float mixAudioFrame(const uint8_t* p,unsigned channels,unsigned bits,bool floating) {
    if(!channels) return 0;
    float sum=0; for(unsigned c=0;c<channels;++c) sum+=decodeAudioSample(p+c*((bits+7)/8),bits,floating);
    return sum/channels;
}
}
