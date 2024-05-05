#pragma once
#include "lvgl.h"
#include <functional>

typedef std::function<void(const int newtemp)> SetTemperatureCallback;
typedef std::function<void()> CancelCallback;
class TOsk {
  private:
    char FTempIntro[5] = {0,0,0,0,0}; //4 chars input (xx.x) plus terminator
    int FTempIndex; //current input character
    lv_obj_t *FKeyboard; //the keyboard
    lv_obj_t *FTempTextarea; //the textarea showing the temperature
    lv_obj_t *FBtnIncrement; //increment temperature button
    lv_obj_t *FBtnDecrement; //decrement temperature button
    SetTemperatureCallback FSetTemperature; //callback to set the temperature
    CancelCallback FCancelCallback; //callback to cancel temperature introduction
    bool EnableKeybButtons();
    void ShowInputTemp();
  public:
    TOsk(lv_obj_t *keyboard, lv_obj_t *temptextarea, lv_obj_t *btnincrement, lv_obj_t *btndecrement, SetTemperatureCallback settemperature, CancelCallback showtemperature);
    void KeyboardClick(lv_event_t *e);
    void TemperatureClick(lv_event_t *e);
};