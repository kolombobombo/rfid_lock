/*************************************************************
  Download latest Blynk library here:
    https://github.com/blynkkk/blynk-library/releases/latest
 *************************************************************/
String strData = "";
boolean recievedFlag;

#define BLYNK_PRINT Serial
#define RELE 0 //определяем цифровой пин для реле (esp01)
#define GPIO2 2  // определяем цифровой пин для датчика(esp01)
#define RELAY_DELAY 300 // delay for relay

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <TimeLib.h>
#include <WidgetRTC.h>

char auth[] = "3qMkZ3iula7M4vq6Txm5qz4TXCh0t4zc";	//Забить Ваши данные
char ssid[] = "main01";			//Забить Ваши данные
char pass[] = "oblomoblom";			//Забить Ваши данные

WidgetRTC rtc;
WidgetTerminal terminal(V100);

BLYNK_CONNECTED() {
  // Synchronize time on connection
  rtc.begin();
  Blynk.syncVirtual(V0);
  Blynk.syncVirtual(V2);
}

BLYNK_WRITE(V0)
{
  int pinGPIO0 = param.asInt(); 
  if (pinGPIO0 == HIGH) {
    Serial.print("open_door_now");
  } 
  digitalWrite(RELE, pinGPIO0);
  delay(RELAY_DELAY);
  digitalWrite(RELE, LOW);
  
}

BLYNK_WRITE(V2)
{
  int pinGPIO2 = param.asInt(); 
  digitalWrite(GPIO2, pinGPIO2);
}

BLYNK_WRITE(V100)
{
  // if you type "open" into Terminal Widget - 
  if (String("open") == param.asStr()) {
    //terminal_print_date_time();
    terminal.println("Data TX to Arduino:") ;
    Serial.print("open_door_now");
  } else {
    //terminal_print_date_time();
    terminal.print(" RX:");
    terminal.write(param.getBuffer(), param.getLength());
    terminal.println();
    //digitalWrite (RELE, LOW);
    }

  // Ensure everything is sent
  terminal.flush();
}



void setup()
{
  Serial.begin(9600);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  setSyncInterval(10 * 60); // Sync interval in seconds (10 minutes) 
    
  pinMode(RELE, OUTPUT);
  pinMode(GPIO2, OUTPUT);
 
  Blynk.begin(auth, ssid, pass);
  
  terminal.clear();
  terminal.print("Startup : ");
  terminal_print_date_time();
  terminal.println(F("Blynk v" BLYNK_VERSION  ": Device started")); 
  terminal.flush();
}

void loop()
{
  Blynk.run();
  
 while (Serial.available() > 0) {         // ПОКА есть что то на вход    
    strData += (char)Serial.read();        // забиваем строку принятыми данными
    recievedFlag = true;                   // поднять флаг что получили данные
    delay(2);                              // ЗАДЕРЖКА. Без неё работает некорректно!
  }
  if (recievedFlag) {                      // если данные получены
    //terminal.println(strData);               // вывести
    //terminal_print_date_time();
    Blynk.virtualWrite(V100, strData);
    strData = "";                          // очистить
    recievedFlag = false;                  // опустить флаг
  }  

}

//A function to print current date and time to Blynk terminal widget, gets called from the below functions
void terminal_print_date_time()
{
  String currentDate = String(day()) + "." + month() + "." + year();
  String currentTime = String(hour()) + ":" + minute() + ":" + second();
  terminal.print(currentDate);
  terminal.print(" ");
  terminal.print(currentTime);
  terminal.println();
}
