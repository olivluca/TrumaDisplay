#include "osk.hpp"
#include <stdlib.h>
#include <string.h>


enum buttons {btBackspace, btOk, btCancel ,bt1, bt2, bt3, bt4, bt5, bt6, bt7, bt8, bt9, bt0, btDecimal};


const char * KeyMap[] = {LV_SYMBOL_BACKSPACE,LV_SYMBOL_OK,LV_SYMBOL_CLOSE,"\n",
                        "1","2","3","\n",
                        "4","5","6","\n",
                        "7","8","9","\n",
                        "0",".",
                        ""};

TOsk::TOsk(lv_obj_t *keyboard, lv_obj_t *tempTextarea, lv_obj_t *btnIncrement, lv_obj_t *btnDecrement, SetTemperatureCallback setTemperature, CancelCallback cancelCallback)
{
  FKeyboard=keyboard;
  FTempTextarea=tempTextarea;
  FBtnIncrement=btnIncrement;
  FBtnDecrement=btnDecrement;  
  FSetTemperature=setTemperature;  
  FCancelCallback=cancelCallback;
  const lv_buttonmatrix_ctrl_t ctrl_map[]={(lv_buttonmatrix_ctrl_t)1}; //dummy width, EnableKeybButtons will do the real thing
  lv_keyboard_set_map(FKeyboard, LV_KEYBOARD_MODE_NUMBER, KeyMap,ctrl_map);
}

#define ENABLED (lv_buttonmatrix_ctrl_t)(LV_BTNMATRIX_CTRL_NO_REPEAT | LV_BTNMATRIX_CTRL_CLICK_TRIG)
#define DISABLED (lv_buttonmatrix_ctrl_t)(ENABLED | LV_BTNMATRIX_CTRL_DISABLED)
#define ENABLED_WIDTH_1 (lv_buttonmatrix_ctrl_t)(ENABLED | 1)
#define DISABLED_WIDTH_1 (lv_buttonmatrix_ctrl_t)(DISABLED | 1)
bool TOsk::EnableKeybButtons()
{     
    lv_buttonmatrix_ctrl_t KeybFlags[14];
      //backspace enabled with at least one input
      if (FTempIndex>0) {
        KeybFlags[btBackspace]=ENABLED_WIDTH_1;
      } else {
        KeybFlags[btBackspace]=DISABLED_WIDTH_1;
      }

      //ok
      bool okenabled=FTempIndex>0;
      if (okenabled) {
        float newtemp=atof(FTempIntro);
        okenabled=newtemp>=5.0 && newtemp<=30.0;
      }
      if (okenabled) {
        KeybFlags[btOk]=ENABLED_WIDTH_1;
     } else {
        KeybFlags[btOk]=DISABLED_WIDTH_1;
      }
      KeybFlags[btCancel]=ENABLED_WIDTH_1;

      char *dec=strchr(FTempIntro,'.');
      int maxindex;
      if (dec==NULL) {
        maxindex=3;
      } else {
        maxindex=dec-FTempIntro+1;
      }
      //numbers
      int w=1;
      for (int i=bt1; i<=bt0; i++) {
        if (i==bt0) w=2;
        if (FTempIndex<=maxindex) {
          KeybFlags[i]=(lv_buttonmatrix_ctrl_t)(ENABLED | w);
        } else {
          KeybFlags[i]=(lv_buttonmatrix_ctrl_t) (DISABLED | w);
        }
      }
      //decimal point
      if (dec==NULL && FTempIndex>0) {
        KeybFlags[btDecimal]=ENABLED_WIDTH_1;
      } else  {
        KeybFlags[btDecimal]=DISABLED_WIDTH_1;
      }
    lv_buttonmatrix_set_ctrl_map(FKeyboard, KeybFlags);
    return FTempIndex<=maxindex;
}

void TOsk::ShowInputTemp()
{
  lv_textarea_set_text(FTempTextarea, FTempIntro);
  if (EnableKeybButtons())
    lv_textarea_add_text(FTempTextarea,"_");
}


void TOsk::KeyboardClick(lv_event_t *e)
{
  int keyindex = *(int*)lv_event_get_param(e);
  switch(keyindex) {
    case btBackspace: 
        if (FTempIndex>0) {
        FTempIndex--;
        FTempIntro[FTempIndex]=0;
        ShowInputTemp();
        }
        break;
    case btOk:
        FSetTemperature(atof(FTempIntro)*10);
        lv_obj_add_flag(FKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(FBtnIncrement, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(FBtnDecrement, LV_OBJ_FLAG_CLICKABLE);
        break;    
    case btCancel:
        FCancelCallback();
        lv_obj_add_flag(FKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(FBtnIncrement, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(FBtnDecrement, LV_OBJ_FLAG_CLICKABLE);
        break;    
    case bt1:
    case bt2:
    case bt3:
    case bt4:
    case bt5:
    case bt6:
    case bt7:
    case bt8:
    case bt9:
    case bt0:
        if (FTempIndex<=3) {
            if (keyindex==bt0) {
              FTempIntro[FTempIndex]=0x30;
            } else {
               FTempIntro[FTempIndex]=0x30+keyindex-bt1+1;
            }
            FTempIndex++;
            ShowInputTemp();
        }
        break;
    case btDecimal:
        FTempIntro[FTempIndex]='.';
        FTempIndex++;
        ShowInputTemp();
        break;   
    }
}

void TOsk::TemperatureClick(lv_event_t *e)
{
  if (lv_obj_has_flag(FKeyboard, LV_OBJ_FLAG_HIDDEN)) {
    memset(FTempIntro, 0,sizeof(FTempIntro));
    FTempIndex=0;
    ShowInputTemp();
    lv_obj_clear_flag(FKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(FBtnIncrement, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(FBtnDecrement, LV_OBJ_FLAG_CLICKABLE);
  }
}