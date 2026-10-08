#pragma once
#include "panel.hpp"
namespace bkds::link {
inline constexpr unsigned KeypadDigits[]={153,145,146,147,137,138,139,129,130,131};
inline bool keypad_edit_input(const std::bitset<KeyCount>& keys) {
    if(keys.count()!=1)return false;
    if(keys[140]||keys[156])return true;
    for(unsigned id:KeypadDigits)if(keys[id])return true;
    return false;
}
}
