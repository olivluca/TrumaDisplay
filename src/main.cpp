#include <Arduino.h>
#include <esp32_smartdisplay.h>
#include <esp_lcd_panel_ops.h>
#include <ui/ui.h>
#include <ESP32MQTTClient.h>
#include <WiFi.h>
#include <type_traits>
#include "errtext.h"

//------ you should create your own wifi.h with
//#define WLAN_SSID "your_ssid"
//#define WLAN_PASS "your_password"
//#define MQTT_URI "mqtt://x.x.x.x:1883"
//#define MQTT_USERNAME ""
//#define MQTT_PASSWORD ""
#include "wifi.h"

ESP32MQTTClient  mqttClient;

boolean mqttok=false;
boolean linok=true;
boolean doingreset=false;
boolean showingError=false;
boolean tempchanged=false;
boolean screenoff=false;
boolean screenwasoff=false;
boolean noheartbeat=false;
String waterboost="0";
int error=0;

unsigned long tempdelay;
unsigned long refreshdelay;
unsigned long lastheartbeat;

boolean wifistarted=false;
unsigned long lastwifi;

String BoilerValues[] = {"off","eco","high","boost"};
String FanValues[] = {"eco","high","off","1","2","3","4","5","6","7","8","9","10"};
String FanEnabled[] {"Eco\nHigh", "Eco\nHigh\nOff\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10"};

//only enable the fan when the boiler is off or the heating is on
//(with heating off and the boiler on the fan should stay off,
//with heating on it should allow to select either "eco" or "high",
//when both heating and boiler are off a ventilation speed can be selected)
void EnableFan() {
  boolean enable= lv_obj_has_state(ui_Heating, LV_STATE_CHECKED) || lv_dropdown_get_selected(ui_Boiler)==0;
  boolean enabled=lv_obj_has_flag(ui_Fan, LV_OBJ_FLAG_CLICKABLE);
  if (enable!=enabled) {
    if (enable) {
      lv_obj_add_flag(ui_Fan, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_state(ui_Fan, LV_STATE_DISABLED);
    } else {
      lv_obj_clear_flag(ui_Fan, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_state(ui_Fan, LV_STATE_DISABLED);
    }
  }
}

//Enables/disable the manual speed selection for the fan
//when the heating is on only eco or high should be available
void EnableFanSpeed(boolean enable) {
  int index;
  if (enable) {
    index=1;
  } else {
    index=0;
  }
  if (strcmp(FanEnabled[index].c_str(),lv_dropdown_get_options(ui_Fan))!=0) {
    int selection=lv_dropdown_get_selected(ui_Fan);
    lv_dropdown_set_options(ui_Fan,FanEnabled[index].c_str());
    if (!enable && selection>1) {
      selection=0; 
    }
    lv_dropdown_set_selected(ui_Fan,selection);
  }
}

/* I don't know if strcmp is more costly that the mindless string 
   reallocation done by lvgl, but changing the label when it isn't
   really changing doesn't seem right to me 
*/
void SetLabelText(lv_obj_t * obj, const char * text) {
  if (strcmp(text,lv_label_get_text(obj))!=0) {
     lv_label_set_text(obj,text);
  }
}

//Only Shows/Hide an object if it's not already Shown/Hidden
//I wonder why the library doesn't do this itself, I guess
//that's how C/C++ people do things
void Show(lv_obj_t * obj, boolean show) {
  boolean shown=!lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN);
  if (shown!=show) {
    if (show) {
      lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  }   
}

//Only sets the text color if it's  different
void SetTextColor(lv_obj_t * obj, lv_color_t color) {
  lv_color_t oldcolor=lv_obj_get_style_text_color(obj, LV_PART_MAIN);
  if (memcmp(&oldcolor,&color,sizeof(color))!=0) {
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
  }
}

//Uses the waterboost label to either show an error text
//or the waterboost status
void ShowErrorOrWaterboost() {
  String errmsg="";
  if (error>0 && error<=255) {
    errmsg=ErrText[error];
    if (errmsg=="") {
      errmsg="código de error desconocido";
    }
  }
  if (errmsg != "") {
     SetLabelText(ui_Waterboost,errmsg.c_str());
     SetTextColor(ui_Waterboost, LV_COLOR_MAKE(255,0,0));
  } else if (waterboost=="1") {
     SetLabelText(ui_Waterboost, "boost activo, queda 1 minuto");
     SetTextColor(ui_Waterboost, LV_COLOR_MAKE(0,0,0));
  } else if (waterboost!="0") {
     String wbm="boost activo, quedan "+waterboost+" minutos";
     SetLabelText(ui_Waterboost, wbm.c_str());
     SetTextColor(ui_Waterboost, LV_COLOR_MAKE(0,0,0));
  } else {
    SetLabelText(ui_Waterboost, "");
  }
}

//Forces a refresh of the data (TruMinus will send the
//data as soon as it is received, without waiting for
//a change or 10 seconds)
void RequestRefresh() {
  mqttClient.publish("truma/set/refresh","1");
}

/*
The error screen is used either to show a status/error message
or to change to a screen with no elements when there's no activity
(so that when the user clicks on the screen generating activity
it will not push any underlying button)
*/
void ShowErrorOrStatus() {
  boolean error=!wifistarted || !mqttok || !linok || doingreset || noheartbeat;
  if (error || screenoff) {
    //only change screen if it wasn't already loaded
    if (!showingError && !screenwasoff) {
      Serial.println("Loading ErrorScreen");
      lv_scr_load(ui_ErrorScreen);
    }
    screenwasoff=true; 
    showingError=error; //store that it was an error
    if (!wifistarted) {
      SetLabelText(ui_ErrorLabel,"Conectando al wifi, espere...");
    } else if (!mqttok) { //then display the error/status message
      SetLabelText(ui_ErrorLabel,"Conectando al servidor, espere...");
    } else if (noheartbeat ) {
      SetLabelText(ui_ErrorLabel,"No se reciben datos (no heartbeat)"); 
    } else if (!linok) {
      SetLabelText(ui_ErrorLabel,"Error lin bus");
    } else if (doingreset) {
      SetLabelText(ui_ErrorLabel,"Reset error, espere...");
    } else {
      SetLabelText(ui_ErrorLabel,"");
    }
  } else {
    //currently there's no error and the screen must be kept on
    //clear the data if it was an error
    if (showingError || screenwasoff) {
      Serial.print("Loading normal screen ");
      if (showingError) {
        Serial.println("clearing data");
        SetLabelText(ui_RoomTemp,"---");
        SetLabelText(ui_WaterTemp, "---");
        SetLabelText(ui_Voltage, "---");
        Show(ui_RoomDemand, false);
        Show(ui_WaterDemand,false);
        SetLabelText(ui_Window,"---");
        SetLabelText(ui_ErrClass,"");
        SetLabelText(ui_ErrCode,"");
        SetLabelText(ui_Waterboost,"");
        Show(ui_ResetButton, false);
        RequestRefresh();
      } else {
        Serial.println("without clearing data");
      }
      //and load the normal screen
      lv_scr_load(ui_TrumaMainScreen);
      screenwasoff=false;
      showingError=false;
    }
  }
}

//If the checkbox to turn off the screen is checked and there's no activity
//turn off the backlight, otherwise adjust it with the light sensor
float BrightnessCallback() {
  float adapt=smartdisplay_lcd_adaptive_brightness_cds();
  if (lv_disp_get_inactive_time(lv_disp_get_default())>30000 && lv_obj_has_state(ui_ScreenOff, LV_STATE_CHECKED)) {
    if (!screenoff) {
      screenoff=true;
      ShowErrorOrStatus();
    }
    return 0.0;
  }
  if (screenoff) {
    screenoff=false;
    ShowErrorOrStatus();
  }
  return adapt;
}

//----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  smartdisplay_init();
   __attribute__((unused)) auto disp = lv_disp_get_default();
  lv_disp_set_rotation(disp, LV_DISP_ROT_90);
  ui_init();
  smartdisplay_lcd_set_brightness_cb(BrightnessCallback, 100);  
  //starts the wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  lastwifi=millis();

  //starts the mqtt connection to the broker
  mqttClient.setURI(MQTT_URI, MQTT_USERNAME, MQTT_PASSWORD);
  mqttClient.setKeepAlive(30);
  mqttClient.enableLastWillMessage("trumadisplay/lwt", "I am going offline");
  mqttClient.enableDebuggingMessages(false);
  mqttClient.loopStart();
  ShowErrorOrStatus();
  refreshdelay=millis();
}

//Sends the temperature setting to the broker
//(either 0.0 if the switch is of or the selected value if it is on)
void SendTemperature() {
  if (lv_obj_has_state(ui_Heating,LV_STATE_CHECKED)) {
    int t=lv_spinbox_get_value(ui_Temp);
    Serial.print("Temp value ");
    Serial.println(t);
    double value=(double)t/10;
    Serial.print("Sending temperature setpoint ");
    Serial.println(value,1);
    mqttClient.publish("truma/set/temp", String(value,1),0,true);
    EnableFanSpeed(false);
    EnableFan();
  } else {
    Serial.println("Sending 0.0");
    mqttClient.publish("truma/set/temp", "0.0",0,true);
    EnableFanSpeed(true);
    EnableFan();
  }
}

//checks and restart the wifi connection
void CheckWifi() {
  // check wifi connectivity
  if (WiFi.status()==WL_CONNECTED) {
    lastwifi=millis();
    if (!wifistarted) {
      Serial.print("IP address ");
      Serial.println(WiFi.localIP());
      wifistarted=true;
      ShowErrorOrStatus();
    } 
  } else {
    if (wifistarted) {
      Serial.println("Wifi connection lost");
      wifistarted=false;
      mqttok=false;
      ShowErrorOrStatus();
    }
    unsigned long elapsed=millis()-lastwifi;
    if (elapsed>10000) {
    WiFi.reconnect();
    lastwifi=millis();
    }
  }
}

//---------------------------------------------------------------------
void loop() {

  CheckWifi();

  //hack to try and fix the missing redraws of the screen
  //without this hacks, parts of the screen won't be redrawn
  //showing elements, or parts of element, that shouldn't be there
  //it works but it slows everything down.
  //I could use a longer delay, but then the scrolling label
  //will become jumpy, this way at least the scrolling is 
  //always slow with no sudden jumps
  
  if(millis()-refreshdelay>100) {
    refreshdelay=millis();
    lv_obj_invalidate(lv_scr_act());
  } 

  //only send the temperature setpoint after a delay
  //of the selected temperature change or the switch change
  if (tempchanged) {
    if (millis()-tempdelay>1000) {
      tempchanged=false;
      SendTemperature();
    }
  }

  //keep track of the heartbeat only if the broker is connected 
  if (!wifistarted || !mqttok) {
    noheartbeat=false;
    lastheartbeat=millis();
  } else {
    if (!noheartbeat) {
      if (millis()-lastheartbeat>15000) {
        noheartbeat=true;
        ShowErrorOrStatus();
      }
    }
  }

  lv_timer_handler();

  //just because
  delay(10);
}

/* mqtt handling */
esp_err_t handleMQTT(esp_mqtt_event_handle_t event) {
  if (event->event_id==MQTT_EVENT_DISCONNECTED || event->event_id == MQTT_EVENT_ERROR) {
    mqttok=false;
    ShowErrorOrStatus();
  } 
  mqttClient.onEventCallback(event);
  return ESP_OK;
}

// message received from the mqtt broker
void callback(const String& topic, const String& payload) {
  mqttok=true;
  ShowErrorOrStatus();
  Serial.print("Received mqtt message [");
  Serial.print(topic);
  Serial.print("] payload \"");
  Serial.print(payload);
  Serial.println("\"");
  Serial.flush();
  //statuses
  if (topic=="truma/status/heartbeat") {
     if (noheartbeat) {
      noheartbeat=false;
      ShowErrorOrStatus();
     }
     lastheartbeat=millis();
  }
  else if (topic=="truma/status/room_temp") {
    SetLabelText(ui_RoomTemp,payload.c_str());
  }
  else if (topic=="truma/status/water_temp") {
    SetLabelText(ui_WaterTemp,payload.c_str());
  }
  else if (topic=="truma/status/voltage") {
    SetLabelText(ui_Voltage,payload.c_str());
  }
  else if (topic=="truma/status/window") {
    if (payload=="1") {
      SetLabelText(ui_Window,"cerrada");
      SetTextColor(ui_Window, LV_COLOR_MAKE(0,0,0));
    } else {
      SetLabelText(ui_Window,"abierta");
      SetTextColor(ui_Window, LV_COLOR_MAKE(255,0,0));
    }
  }
  else if (topic=="truma/status/roomdemand") {
    Show(ui_RoomDemand, payload=="1");
  }
  else if (topic=="truma/status/waterdemand") {
    Show(ui_WaterDemand, payload=="1");
  }  
  else if (topic=="truma/status/waterboost") {
    waterboost=payload;
    ShowErrorOrWaterboost();
  }
  else if (topic=="truma/status/err_class") {
    boolean showbutton=false;
    if (payload=="0") {
      SetLabelText(ui_ErrClass,"");
    }    
    else if (payload=="1" || payload=="2") {
      SetLabelText(ui_ErrClass,"W");
    }
    else if (payload=="10" || payload=="20" || payload=="30") {
      SetLabelText(ui_ErrClass,"E");
      showbutton=true;
    }
    else if (payload=="40") {
      SetLabelText(ui_ErrClass, "L");
    }
    else {
      SetLabelText(ui_ErrClass, "?");
      showbutton=true;
    }
    Show(ui_ResetButton, showbutton);
  }
  else if (topic=="truma/status/err_code") {
    if (payload=="0") {
      SetLabelText(ui_ErrCode,"");
      error=0;
    } else {
      error=atoi(payload.c_str());
      SetLabelText(ui_ErrCode,payload.c_str());
    }
    ShowErrorOrWaterboost();
  }  
  else if (topic=="truma/status/linok") {
    linok=payload=="1";
    ShowErrorOrStatus();
  }
  else if (topic=="truma/status/reset") {
    doingreset=payload=="1";
    ShowErrorOrStatus();
  }

  //setpoints (changed somewhere else and reflected here)
  else if (topic=="truma/set/temp") {
    double temp=atof(payload.c_str());
    if (temp<5.0) {
       lv_obj_clear_state(ui_Heating,LV_STATE_CHECKED);
       EnableFanSpeed(true);
       EnableFan();
    } else {
       lv_obj_add_state(ui_Heating,LV_STATE_CHECKED);
       lv_spinbox_set_value(ui_Temp, temp*10);
       EnableFanSpeed(false);
       EnableFan();
    }
  }
  else if (topic=="truma/set/boiler") {
    for (int i=0; i<std::extent<decltype(BoilerValues)>::value; i++) {
      if (payload==BoilerValues[i]) {
        lv_dropdown_set_selected(ui_Boiler,i);
        EnableFan();
        break;
      }
    }
  }
  else if (topic=="truma/set/fan") {
    for (int i=0; i<std::extent<decltype(FanValues)>::value; i++) {
      if (payload==FanValues[i]) {
        lv_dropdown_set_selected(ui_Fan,i);
        break;
      }
    }
  }
}

// connection to the broker established, subscribe to the settings and
// force publish the next received data
void onConnectionEstablishedCallback(esp_mqtt_client_handle_t client) {
  //doforcesend=true;
  mqttok=true;
  linok=true;
  doingreset=false;
  ShowErrorOrStatus();
  mqttClient.subscribe("truma/#", callback);
  RequestRefresh();
}

//Error reset button clicked
void ResetError(lv_event_t * e) {
  Serial.println("Reset clicked, sending truma/set/error_reset");
  mqttClient.publish("truma/set/error_reset","1");
}

//Increments or decrements the temperature setpoint
void ChangeTemp(int increment) {
  tempchanged=true;
  tempdelay=millis();
  int temp=lv_spinbox_get_value(ui_Temp)+increment;
  if (temp<50) {
    temp=50;
  }
  if (temp>300) {
    temp=300;
  }
  lv_spinbox_set_value(ui_Temp,temp);
}

//Button + clicked or long pressed (repeat)
void IncrTemperature(lv_event_t * e) {
  Serial.println("IncrTemperature");
  ChangeTemp(1);
}

//Button - clicked or long pressed (repeat)
void DecrTemperature(lv_event_t * e) {
  Serial.println("DecrTemperature");
  ChangeTemp(-1);
}

//Boiler selection changed
void WaterChanged(lv_event_t * e)
{
  Serial.println("WaterChanged");
  int value=lv_dropdown_get_selected(ui_Boiler);
  if (value>=0 && value<std::extent<decltype(BoilerValues)>::value) {
    mqttClient.publish("truma/set/boiler",BoilerValues[value],0,true);
    EnableFan();
  }
}

//Fan selection changed
void FanChanged(lv_event_t * e)
{
  Serial.println("FanChanged");
  int value=lv_dropdown_get_selected(ui_Fan);
  if (value>=0 && value<std::extent<decltype(FanValues)>::value) {
    mqttClient.publish("truma/set/fan",FanValues[value],0,true);
  }
}

//Heating switch changed
void HeatingOn(lv_event_t * e)
{
  Serial.println("HeatingOn");
  tempchanged=true;
  tempdelay=millis();
}
