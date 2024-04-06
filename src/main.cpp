#include <Arduino.h>
#include <esp32_smartdisplay.h>
#include <ui/ui.h>
#include <ESP32MQTTClient.h>
#include <WiFi.h>
#include <type_traits>

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
boolean resetenabled=false;

unsigned long tempdelay;

String BoilerValues[] = {"off","eco","high","boost"};
String FanValues[] = {"off","eco","high","1","2","3","4","5","6","7","8","9","10"};

void ShowResetButton(boolean show) {
  resetenabled=show;
  if (show) {
    lv_obj_clear_flag(ui_ResetButton, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui_ResetButton, LV_OBJ_FLAG_HIDDEN);
  }
}

void RequestRefresh() {
  mqttClient.publish("truma/set/refresh","1");
}

void ShowError() {
  if (mqttok && linok && !doingreset) {
    if (showingError) {
      showingError=false;
      lv_label_set_text(ui_RoomTemp,"---");
      lv_label_set_text(ui_WaterTemp, "---");
      lv_label_set_text(ui_Voltage, "---");
      lv_obj_add_flag(ui_RoomDemand, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_WaterDemand,LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui_Window,"---");
      lv_label_set_text(ui_ErrClass,"");
      lv_label_set_text(ui_ErrCode,"");
      lv_label_set_text(ui_Waterboost,"");
      lv_scr_load(ui_TrumaMainScreen);
      ShowResetButton(false);
      RequestRefresh();
    }
    return;
  }
  if (!mqttok) {
    lv_label_set_text(ui_ErrorLabel,"Conectando, espere...");
  } else if (!linok) {
    lv_label_set_text(ui_ErrorLabel,"Error lin bus");
  } else {
    lv_label_set_text(ui_ErrorLabel,"Reset error, espere...");
  }
  if (!showingError) {
    showingError=true;
    lv_scr_load(ui_ErrorScreen);
  }
}

void setup() {
  Serial.begin(115200);
  smartdisplay_init();
   __attribute__((unused)) auto disp = lv_disp_get_default();
 lv_disp_set_rotation(disp, LV_DISP_ROT_90);
   ui_init();
 //starts the wifi (loop will check if it's connected)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASS);

  //starts the mqtt connection to the broker
  mqttClient.setURI(MQTT_URI, MQTT_USERNAME, MQTT_PASSWORD);
  mqttClient.setKeepAlive(30);
  mqttClient.enableLastWillMessage("trumadisplay/lwt", "I am going offline");
  mqttClient.enableDebuggingMessages(false);
  mqttClient.loopStart();
  ShowError();
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
  lv_timer_handler();

  if (tempchanged) {
    if (millis()-tempdelay>1000) {
      tempchanged=false;
      SendTemperature();
    }
  }
}

/* mqtt handling */
esp_err_t handleMQTT(esp_mqtt_event_handle_t event) {
  if (event->event_id==MQTT_EVENT_DISCONNECTED || event->event_id == MQTT_EVENT_ERROR) {
    mqttok=false;
    ShowError();
  } 
  mqttClient.onEventCallback(event);
  return ESP_OK;
}

// message received from the mqtt broker
void callback(const String& topic, const String& payload) {
  mqttok=true;
  ShowError();
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
    if (payload=="0") {
      lv_label_set_text(ui_Waterboost,"");
    } else if (payload=="1") {
      lv_label_set_text(ui_Waterboost, "boost activo, queda 1 minuto"); 
    } else {
      String msg="boost activo, quedan "+payload+" minutos";
      lv_label_set_text(ui_Waterboost, msg.c_str());
    }
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
    } else {
      lv_label_set_text(ui_ErrCode, payload.c_str());
    }
  }  
  else if (topic=="truma/status/linok") {
    linok=payload=="1";
    ShowError();
  }
  else if (topic=="truma/status/reset") {
    doingreset=payload=="1";
    ShowError();
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
  ShowError();
  mqttClient.subscribe("truma/#", callback);
  RequestRefresh();
}


void ResetError(lv_event_t * e) {
  Serial.print("Reset clicked");
  if (!resetenabled) {
    Serial.println(" ignored");
  } else {
    Serial.println("sending truma/set/error_reset");
    mqttClient.publish("truma/set/error_reset","1");
  }
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
