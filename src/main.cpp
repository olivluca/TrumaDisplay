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
String waterboost="0";
int error=0;
boolean screenchanged=false;

unsigned long tempdelay;
unsigned long refreshdelay;

String BoilerValues[] = {"off","eco","high","boost"};
String FanValues[] = {"off","eco","high","1","2","3","4","5","6","7","8","9","10"};


void ShowResetButton(boolean show) {
  if (show) {
    lv_obj_clear_flag(ui_ResetButton, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui_ResetButton, LV_OBJ_FLAG_HIDDEN);
  }
}

void ShowErrorOrWaterboost() {
  String errmsg="";
  if (error>0 && error<=255) {
    errmsg=ErrText[error];
  }
  if (errmsg != "") {
     lv_label_set_text(ui_Waterboost,errmsg.c_str());
     lv_obj_set_style_text_color(ui_Waterboost, LV_COLOR_MAKE(255,0,0), 0);
  } else if (waterboost=="1") {
     lv_label_set_text(ui_Waterboost, "boost activo, queda 1 minuto");
     lv_obj_set_style_text_color(ui_Waterboost, LV_COLOR_MAKE(0,0,0), 0);
  } else if (waterboost!="0") {
     String wbm="boost activo, quedan "+waterboost+" minutos";
     lv_label_set_text(ui_Waterboost, wbm.c_str());
     lv_obj_set_style_text_color(ui_Waterboost, LV_COLOR_MAKE(0,0,0), 0);
  } else {
    lv_label_set_text(ui_Waterboost, "");
  }
}

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
  boolean error=!mqttok || !linok || doingreset;
  if (error || screenoff) {
    //only change screen if it wasn't already loaded
    if (!showingError && !screenwasoff) {
      Serial.println("Loading ErrorScreen");
      lv_scr_load(ui_ErrorScreen);
      screenchanged=true;
      refreshdelay=millis();
    }
    screenwasoff=true; 
    showingError=error; //store that it was an error
    if (!mqttok) { //then display the error/status message
      lv_label_set_text(ui_ErrorLabel,"Conectando, espere...");
    } else if (!linok) {
      lv_label_set_text(ui_ErrorLabel,"Error lin bus");
    } else if (doingreset) {
      lv_label_set_text(ui_ErrorLabel,"Reset error, espere...");
    } else {
      lv_label_set_text(ui_ErrorLabel,"");
    }
  } else {
    //currently there's no error and the screen must be kept on
    //clear the data if it was an error
    if (showingError || screenwasoff) {
      Serial.print("Loading normal screen ");
      if (showingError) {
        Serial.println("clearing data");
        lv_label_set_text(ui_RoomTemp,"---");
        lv_label_set_text(ui_WaterTemp, "---");
        lv_label_set_text(ui_Voltage, "---");
        lv_obj_add_flag(ui_RoomDemand, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WaterDemand,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_Window,"---");
        lv_label_set_text(ui_ErrClass,"");
        lv_label_set_text(ui_ErrCode,"");
        lv_label_set_text(ui_Waterboost,"");
        ShowResetButton(false);
        RequestRefresh();
      } else {
        Serial.println("without clearing data");
      }
      //and load the normal screen
      lv_scr_load(ui_TrumaMainScreen);
      screenchanged=true;
      refreshdelay=millis();
      screenwasoff=false;
      showingError=false;
    }
  }
}

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

void setup() {
  Serial.begin(115200);
  smartdisplay_init();
   __attribute__((unused)) auto disp = lv_disp_get_default();
 lv_disp_set_rotation(disp, LV_DISP_ROT_90);
   ui_init();
 smartdisplay_lcd_set_brightness_cb(BrightnessCallback, 100);  
 //starts the wifi (loop will check if it's connected)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASS);

  //starts the mqtt connection to the broker
  mqttClient.setURI(MQTT_URI, MQTT_USERNAME, MQTT_PASSWORD);
  mqttClient.setKeepAlive(30);
  mqttClient.enableLastWillMessage("trumadisplay/lwt", "I am going offline");
  mqttClient.enableDebuggingMessages(false);
  mqttClient.loopStart();
  ShowErrorOrStatus();
  refreshdelay=millis();
}


void SendTemperature() {
  if (lv_obj_has_state(ui_Heating,LV_STATE_CHECKED)) {
    int t=lv_spinbox_get_value(ui_Temp);
    Serial.print("Temp value ");
    Serial.println(t);
    double value=(double)t/10;
    Serial.print("Sending temperature setpoint ");
    Serial.println(value,1);
    mqttClient.publish("truma/set/temp", String(value,1),0,true);
  } else {
    Serial.println("Sending 0.0");
    mqttClient.publish("truma/set/temp", "0.0",0,true);
  }
}

void loop() {

  //hack to try and fix the missing redraws of the screen
  
  if(screenchanged && millis()-refreshdelay>500) {
    screenchanged=false;
    lv_obj_invalidate(lv_scr_act());
  } 

  if (tempchanged) {
    if (millis()-tempdelay>1000) {
      tempchanged=false;
      SendTemperature();
    }
  }
  lv_timer_handler();
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
  if (topic=="truma/status/room_temp") {
    lv_label_set_text(ui_RoomTemp,payload.c_str());
  }
  else if (topic=="truma/status/water_temp") {
    lv_label_set_text(ui_WaterTemp,payload.c_str());
  }
  else if (topic=="truma/status/voltage") {
    lv_label_set_text(ui_Voltage,payload.c_str());
  }
  else if (topic=="truma/status/window") {
    if (payload=="1") {
      lv_label_set_text(ui_Window,"cerrada");
      lv_obj_set_style_text_color(ui_Window, LV_COLOR_MAKE(0,0,0), 0);
    } else {
      lv_label_set_text(ui_Window,"abierta");
      lv_obj_set_style_text_color(ui_Window, LV_COLOR_MAKE(255,0,0), 0);
    }
  }
  else if (topic=="truma/status/roomdemand") {
    if (payload=="1") {
      lv_obj_clear_flag(ui_RoomDemand, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ui_RoomDemand, LV_OBJ_FLAG_HIDDEN);
    }
  }
  else if (topic=="truma/status/waterdemand") {
    if (payload=="1") {
      lv_obj_clear_flag(ui_WaterDemand, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ui_WaterDemand, LV_OBJ_FLAG_HIDDEN);
    }
  }
  else if (topic=="truma/status/waterboost") {
    waterboost=payload;
    ShowErrorOrWaterboost();
  }
  else if (topic=="truma/status/err_class") {
    boolean showbutton=false;
    if (payload=="0") {
      lv_label_set_text(ui_ErrClass,"");
    }    
    else if (payload=="1" || payload=="2") {
      lv_label_set_text(ui_ErrClass,"W");
    }
    else if (payload=="10" || payload=="20" || payload=="30") {
      lv_label_set_text(ui_ErrClass,"E");
      showbutton=true;
    }
    else if (payload=="40") {
      lv_label_set_text(ui_ErrClass, "L");
    }
    else {
      lv_label_set_text(ui_ErrClass, "?");
      showbutton=true;
    }
    ShowResetButton(showbutton);
  }
  else if (topic=="truma/status/err_code") {
    if (payload=="0") {
      lv_label_set_text(ui_ErrCode,"");
      error=0;
    } else {
      error=atoi(payload.c_str());
      lv_label_set_text(ui_ErrCode,payload.c_str());
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


  else if (topic=="truma/set/temp") {
    double temp=atof(payload.c_str());
    if (temp<5.0) {
       lv_obj_clear_state(ui_Heating,LV_STATE_CHECKED);
    } else {
       lv_obj_add_state(ui_Heating,LV_STATE_CHECKED);
       lv_spinbox_set_value(ui_Temp, temp*10);
    }
  }
  else if (topic=="truma/set/boiler") {
    for (int i=0; i<std::extent<decltype(BoilerValues)>::value; i++) {
      if (payload==BoilerValues[i]) {
        lv_dropdown_set_selected(ui_Boiler,i);
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

void ResetError(lv_event_t * e) {
  Serial.println("Reset clicked, sending truma/set/error_reset");
  mqttClient.publish("truma/set/error_reset","1");
}

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

void IncrTemperature(lv_event_t * e) {
  Serial.println("IncrTemperature");
  ChangeTemp(1);
}

void DecrTemperature(lv_event_t * e) {
  Serial.println("DecrTemperature");
  ChangeTemp(-1);
}

void WaterChanged(lv_event_t * e)
{
  Serial.println("WaterChanged");
  int value=lv_dropdown_get_selected(ui_Boiler);
  if (value>=0 && value<std::extent<decltype(BoilerValues)>::value) {
    mqttClient.publish("truma/set/boiler",BoilerValues[value],0,true);
  }
}

void FanChanged(lv_event_t * e)
{
  Serial.println("FanChanged");
  int value=lv_dropdown_get_selected(ui_Fan);
  if (value>=0 && value<std::extent<decltype(FanValues)>::value) {
    mqttClient.publish("truma/set/fan",FanValues[value],0,true);
  }
}


void HeatingOn(lv_event_t * e)
{
  Serial.println("HeatingOn");
  tempchanged=true;
  tempdelay=millis();
}
