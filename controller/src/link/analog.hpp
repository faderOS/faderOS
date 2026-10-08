#pragma once
#include <cstdint>
namespace bkds::link {
struct AnalogRange {
    int low=0,center=128,high=255;
    bool valid(bool bipolar) const { return low>=0&&low<high&&(!bipolar||(low<center&&center<high)); }
    int normalize(int raw,bool bipolar,int deadband=0) const {
        if(raw<low) raw=low;
        if(raw>high) raw=high;
        if(!bipolar) return (raw-low)*4095/(high-low);
        if(raw>=center-deadband&&raw<=center+deadband) return 0;
        return raw<center?-(center-deadband-raw)*32767/(center-deadband-low)
                         :(raw-center-deadband)*32767/(high-center-deadband);
    }
};
struct AnalogCalibration {
    AnalogRange tbar{0,2048,4095},x{},y{};
    int tbar_margin=0,joystick_deadband=0;
    bool valid() const {
        return tbar.low>=-4095&&tbar.high<=4095&&tbar.low<tbar.high&&tbar.high-tbar.low<=4095
            &&tbar_margin>=0&&tbar_margin<=2047&&2*tbar_margin<tbar.high-tbar.low
            &&x.valid(true)&&x.high<=255&&y.valid(true)&&y.high<=255
            &&joystick_deadband>=0&&joystick_deadband<x.center-x.low&&joystick_deadband<x.high-x.center
            &&joystick_deadband<y.center-y.low&&joystick_deadband<y.high-y.center;
    }
    // Select the equivalent counter value closest to the calibrated interval.
    // No temporal unwrapping: reconnects and dropped samples need no history.
    int tbar_unwrap(int raw) const {
        if(raw>tbar.high && raw-tbar.high>tbar.low-(raw-4096)) raw-=4096;
        else if(raw<tbar.low && tbar.low-raw>(raw+4096)-tbar.high) raw+=4096;
        return raw;
    }
    int tbar_value(int raw) const {
        AnalogRange effective{tbar.low+tbar_margin,0,tbar.high-tbar_margin};
        return effective.normalize(tbar_unwrap(raw),false);
    }
    int x_value(int raw) const {return x.normalize(raw,true,joystick_deadband);}
    int y_value(int raw) const {return y.normalize(raw,true,joystick_deadband);}
};
}
