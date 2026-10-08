#pragma once
#include <cstring>
namespace bkds::link {
// Stock obs-transitions/data/luma_wipes/wipes.json and English labels.
struct LumaPattern { const char* file; const char* label; };
inline constexpr LumaPattern LumaPatterns[]={
    {"barndoor-botleft.png","Barn door Bottom left"},
    {"barndoor-h.png","Barn door horizontal"},
    {"barndoor-topleft.png","Barn door Top left"},
    {"barndoor-v.png","Barn door vertical"},
    {"blinds-h.png","Blinds horizontal"},
    {"box-botleft.png","Box Bottom left"},
    {"box-botright.png","Box Bottom right"},
    {"box-topleft.png","Box Top left"},
    {"box-topright.png","Box Top right"},
    {"burst.png","Burst"},
    {"checkerboard-small.png","Checkerboard Small"},
    {"circles.png","Circles"},
    {"clock.png","Clock"},
    {"cloud.png","Cloud"},
    {"curtain.png","Curtain"},
    {"fan.png","Fan"},
    {"fractal.png","Fractal"},
    {"iris.png","Iris"},
    {"linear-h.png","Linear horizontal"},
    {"linear-topleft.png","Linear Top left"},
    {"linear-topright.png","Linear Top right"},
    {"linear-v.png","Linear vertical"},
    {"parallel-zigzag-h.png","Parallel Zigzag horizontal"},
    {"parallel-zigzag-v.png","Parallel Zigzag vertical"},
    {"sinus9.png","Sine 9"},
    {"spiral.png","Spiral"},
    {"square.png","Square"},
    {"squares.png","Squares"},
    {"stripes.png","Stripes"},
    {"strips-h.png","Strips horizontal"},
    {"strips-v.png","Strips vertical"},
    {"watercolor.png","Watercolor"},
    {"zigzag-h.png","Zigzag horizontal"},
    {"zigzag-v.png","Zigzag vertical"},
};
inline bool valid_luma_pattern(const char* file) {
    for(const auto& p:LumaPatterns) if(std::strcmp(file,p.file)==0) return true;
    return false;
}
inline const char* default_luma_pattern(unsigned code) {
    switch(code) {
    case 0: case 1: return "linear-h.png";
    case 3: return "linear-v.png";
    // Black pixels reveal PGM first: OBS box filenames name the opposite corner.
    case 5: return "box-botright.png";
    case 6: return "box-botleft.png";
    case 9: return "linear-topleft.png";
    case 18: return "barndoor-v.png";
    case 17: return "barndoor-h.png";
    case 21: return "square.png";
    case 24: return "iris.png";
    default: return ""; // No stock diamond mask: do not silently substitute a square.
    }
}
}
