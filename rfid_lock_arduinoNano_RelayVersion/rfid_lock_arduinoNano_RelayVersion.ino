/*
   Электронный замок с бесконтактным доступом по технологии RFID. На основе проекта https://kit.alexgyver.ru/tutorials/rfid-lock/
   Использован RFID модуль MFRC522
   Индикация состояния - зеленый и красный светодиод (можно использовать сдвоенный или RGB). Установка не обязательно
   Подача звукового сигнала при помощи баззера. Можно заменить на светодиод. Установка не обязательна, но желательна.
   Остальные функции реализуются при помощи кнопки на внутренней стороне двери. Кнопка обязательно, чтобы добавлять/удалять ключи.
   В качестве механизма открытия двери используется реле к которому будет подключен электромеханический замок. 
   Реле кратковременно замыкает 12В на реле, тем самым открывает защелку электромеханического замка
	Концевик двери не обязателен, показывает статус двери закрыта/открыта. В качестве концевика может быть кнопка или геркон с магнитом.
	
   Запись нового ключа: поднесите метку при открытой двери и зажатой кнопке до сигнала (2 писка)
   Удаление записанного ключа: аналогично поднесите метку при зажатой кнопке до сигнала (3 писка)
   Удаление всех ключей: зажать кнопку на 4 секунды после подачи питания до сигнала

	Подключение
		Реле low level trigger: D10
		Зуммер: D3
		Красный светодиод: D4
		Зелёный светодиод: D5
		Кнопка открытия/записи: D8
		Концевик двери: D9
		RFID RC522
			3.3V: 3V3
			RST: D6
			GND: GND
			MISO: D12
			MOSI: D11
			SCK: D13
			SDA: D7

*/
String strData = "";                        //for RX Data
boolean recievedFlag;                       //for RX Data
static uint32_t OpenDoorTimer = 0;          // Таймер открытой двери

#include <SPI.h>          // Библиотека SPI для MFRC522    
#include <MFRC522.h>      // Библиотека RFID модуля MFRC522
#include <EEPROM.h>       // Библиотека EEPROM для хранения ключей
#include <TimerMs.h>

#define LOCK_TIMEOUT  1000  // Время до индикации закрытой  двери, в мс 
#define DOOR_TIMEOUT  60000  // Время до сообщения об открытой двери, в мс 
#define RELAY_DELAY 100   // Время замыкания реле, в мс 
#define MAX_TAGS        5  // Максимальное количество хранимых меток - ключей 
#define RELAY_PIN       10   // Пин реле
#define BUZZER_PIN      3   // Пин баззера
#define RED_LED_PIN     4   // Пин красного светодиода
#define GREEN_LED_PIN   5   // Пин зеленого светодиода
#define RST_PIN         6   // Пин RST MFRC522
#define CS_PIN          7   // Пин SDA MFRC522
#define BTN_PIN         8   // Пин кнопки
#define DOOR_PIN        9   // Пин концевика двери, подтянут к VCC. Служит
#define EE_START_ADDR   0   // Начальный адрес в EEPROM
#define EE_KEY        100   // Ключ EEPROM, для проверки на первое вкл.

MFRC522 rfid(CS_PIN, RST_PIN);  // Обьект RFID


#define DECLINE 0   // Отказ
#define SUCCESS 1   // Успешно
#define SAVED   2   // Новая метка записана
#define DELITED 3   // Метка удалена
#define BUTTON 4    // кнопка

bool isOpen(void) {             // Функция должна возвращать true, если дверь физически открыта
  return digitalRead(DOOR_PIN); // Если дверь открыта - концевик размокнут, на пине HIGH
}

void unlock(void) {               // Функция должна разблокировать замок
  digitalWrite(RELAY_PIN, LOW);   //подаем на реле low level trigger, замыкаем
  delay(RELAY_DELAY);				//время замыкания цепи на электромеханический замок, в мс
  digitalWrite(RELAY_PIN, HIGH);	//подаем на реле HIGH level trigger, размыкаем
  delay(100);
  Serial.println("relay low level trigger " + String(RELAY_DELAY)+ "ms.");
}


bool locked = true;       // Флаг состояния замка
bool opened = true;       // Флаг состояния замка
bool needLock = false;    // Служебный флаг
uint8_t savedTags = 0;    // кол-во записанных меток

void setup() {
  // Инициализируем все
  Serial.begin(9600);       // Initialize serial communications with the PC
  SPI.begin();              // Init SPI bus
  rfid.PCD_Init();          // Init MFRC522 card

  // Настраиваем пины
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(DOOR_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  // Полная очистка при включении при зажатой кнопке
  uint32_t start = millis();        // Отслеживание длительного удержания кнопки после включения
  bool needClear = 0;               // Чистим флаг на стирание
  while (!digitalRead(BTN_PIN)) {   // Пока кнопка нажата
    if (millis() - start >= 4000) { // Встроенный таймаут на 4 секунды
      needClear = true;             // Ставим флаг стирания при достижении таймаута
      indicate(DELITED);            // Подаем сигнал удаления
      Serial.println("Clear full");
      break;                        // Выходим из цикла
    }
  }

  // Инициализация EEPROM
  if (needClear or EEPROM.read(EE_START_ADDR) != EE_KEY) { // при первом включении или необходимости очистки ключей
    for (uint16_t i = 0; i < EEPROM.length(); i++) EEPROM.write(i, 0x00); // Чистим всю EEPROM
    Serial.println("EEPROM.length = " + String(EEPROM.length()));
    EEPROM.write(EE_START_ADDR, EE_KEY);                   // Пишем байт-ключ
  } else {                                                 // Обычное включение
    savedTags = EEPROM.read(EE_START_ADDR + 1);            // Читаем кол-во меток в памяти
    Serial.println("savedTags = " + String(savedTags));
  }

  // Начальное состояние замка
  if (savedTags > 0) {      // Если метки в памяти есть
    if (isOpen()) {         // И дверь сейчас открыта
      Serial.println("Door is open");
      ledSetup(SUCCESS);    // Зеленый лед
      locked = false;       // Замок открыт

    } else {                // Метки есть, но дверь закрыта
      Serial.println("Door is close");
      ledSetup(DECLINE);    // Красный лед
      locked = true;        // Замок закрыт

    }
  } else {                  // Если меток не записано
    Serial.println("savedTags = 0");
    ledSetup(SUCCESS);      // Зеленый лед
    locked = false;         // Замок разлочен
    unlock();               // На всякий случай разблокируем замок
  }

}


  
void loop() {
  static uint32_t lockTimeout;             // Таймер таймаута для блокировки замка
  static uint32_t DoorTimeout; // Таймаут двери

 // Принимаем данные по UART
 while (Serial.available() > 0) {         // ПОКА есть что то на вход    
    strData += (char)Serial.read();        // забиваем строку принятыми данными
    recievedFlag = true;                   // поднять флаг что получили данные
    delay(2);                              // ЗАДЕРЖКА. Без неё работает некорректно!
  }
  if (recievedFlag) {                      // если данные получены
    Serial.println("Data RX in Arduino Nano: " + strData);
    if (strData == "open_door_now"){      // Открытие по фразе из Serial-порта
      unlock();
      }
    strData = "";                          // очистить
    recievedFlag = false;                  // опустить флаг
  }  


  // Открытие по нажатию кнопки изнутри
  static uint32_t buttonTimeout; // Таймаут кнопки
  if (!digitalRead(BTN_PIN)) {  // Если нажали кнопку
      if (millis() - buttonTimeout >= 200) {
      Serial.println("button press");
      unlock();                              // Разблокируем замок
      indicate(BUTTON);                     // Зеленый лед
      lockTimeout = millis();                // Запомнили время
      locked = false;                        // Замок разлочен
      }
    buttonTimeout = millis(); 
  }

  // Проверка концевика двери
  if (isOpen()) {                          // Если дверь открыта  
    lockTimeout = millis();                // Обновляем таймер
    // Сообщаем что дверь открыта
    if (!opened) {
      ledSetup(SUCCESS); // Зеленый лед
      Serial.println("Door is open");
      locked = false;     
      opened = true;    // Ставим флаг открытой двери
      OpenDoorTimer = 0;
      DoorTimeout = millis(); 
     } else {
        if (millis() - DoorTimeout >= DOOR_TIMEOUT) {  
          OpenDoorTimer = OpenDoorTimer + ((millis() - DoorTimeout)/1000);
          Serial.println("Door is open "+String(OpenDoorTimer)+" s.");       // Сообщаем если дверь открыта больше DOOR_TIMEOUT сек
          if (OpenDoorTimer == 600){
            Serial.println("Ahtung! Door is open 10 min.");
            }
          DoorTimeout = millis(); 
        }
      }   
   }

 
  // Сообщаем о закрытой двери если концевик замкнут больше значения LOCK_TIMEOUT
  if (!locked and millis() - lockTimeout >= LOCK_TIMEOUT) {
    ledSetup(DECLINE); // Красный лед
    Serial.println("Door is close");
    locked = true;     // Ставим флаг закрытой двери
    opened = false;
    OpenDoorTimer = 0;
  }


  // Поднесение метки
  static uint32_t rfidTimeout; // Таймаут рфид
  if (rfid.PICC_IsNewCardPresent() and rfid.PICC_ReadCardSerial()) { // Если поднесена карта
    if (isOpen() and !digitalRead(BTN_PIN) and millis() - rfidTimeout >= 500) { // И дверь открыта + кнопка нажата
      saveOrDeleteTag(rfid.uid.uidByte, rfid.uid.size);              // Сохраняем или удаляем метку
      Serial.print("SaveOrDelete Tag. ");
      Serial.print(F("Tag Serial:"));
      dump_byte_array(rfid.uid.uidByte, rfid.uid.size);
      Serial.println();
    } else if (millis() - rfidTimeout >= 500) {                      // Иначе если замок заблокирован? , было if (locked or !locked)
      if (foundTag(rfid.uid.uidByte, rfid.uid.size) >= 0) {          // Ищем метку в базе
        Serial.print("Tag is found. ");
        Serial.print(F("Tag UID:"));
        dump_byte_array(rfid.uid.uidByte, rfid.uid.size);
        Serial.println();
        unlock();                                                    // Разблокируем
        indicate(SUCCESS);                                           // Если нашли - подаем сигнал успеха
        lockTimeout = millis();                                      // Обновляем таймаут
        locked = false;                                              // Замок разблокирован
      } else if (millis() - rfidTimeout >= 500) {                    // Метка не найдена (с таймаутом)
        Serial.print(F("Tag is NOT found. Tag UID:"));
        dump_byte_array(rfid.uid.uidByte, rfid.uid.size);             //выводим серийный номер метки
        Serial.println();
        indicate(DECLINE);                                           // Выдаем отказ
      }
    }
    rfidTimeout = millis();                                          // Обвновляем таймаут
  }

  // Перезагружаем RFID каждые 0.5 сек (для надежности)
  static uint32_t rfidRebootTimer = millis(); // Таймер
  if (millis() - rfidRebootTimer > 500) {     // Каждые 500 мс
    rfidRebootTimer = millis();               // Обновляем таймер
    digitalWrite(RST_PIN, HIGH);              // Дергаем резет
    delay(1);
    digitalWrite(RST_PIN, LOW);
    rfid.PCD_Init();                          // Инициализируем модуль
  }
}

// Устанавливаем состояние светодиодов
void ledSetup(bool state) {
  if (state) {  // Зеленый
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, LOW);
  } else {      // Красный
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, HIGH);
  }
}

// Звуковой сигнал + лед
void indicate(uint8_t signal) {
  ledSetup(signal); // Лед
  switch (signal) { // Выбираем сигнал
    case DECLINE:
      Serial.println("RFID FAIL");
      for (uint8_t i = 0; i < 2; i++) {
        tone(BUZZER_PIN, 100);
        delay(300);
        noTone(BUZZER_PIN);
        delay(100);
      }
      return;
    case SUCCESS:
      Serial.println("RFID SUCCESS");
      tone(BUZZER_PIN, 500);
      delay(330);
      noTone(BUZZER_PIN);
      return;
    case BUTTON:
      //Serial.println("BUTTON PRESS");
      tone(BUZZER_PIN, 200);
      delay(330);
      noTone(BUZZER_PIN);
      return;  
    case SAVED:
      Serial.println("RFID SAVED");
      for (uint8_t i = 0; i < 2; i++) {
        tone(BUZZER_PIN, 500);
        delay(330);
        noTone(BUZZER_PIN);
        delay(500);
      }
      return;
    case DELITED:
      Serial.println("RFID DELITED");
      for (uint8_t i = 0; i < 3; i++) {
        tone(BUZZER_PIN, 890);
        delay(330);
        noTone(BUZZER_PIN);
        delay(500);
      }
      return;
  }
}

// Сравнение двух массивов известного размера
bool compareUIDs(uint8_t *in1, uint8_t *in2, uint8_t size) {
  for (uint8_t i = 0; i < size; i++) {  // Проходим по всем элементам
    if (in1[i] != in2[i]) return false; // Если хоть один не сошелся - массивы не совпадают
  }
  return true;                          // Все сошлись - массивы идентичны
}

// Поиск метки в EEPROM
int16_t foundTag(uint8_t *tag, uint8_t size) {
  uint8_t buf[8];   // Буфер метки
  uint16_t address; // Адрес
  for (uint8_t i = 0; i < savedTags; i++) { // проходим по всем меткам 
    address = (i * 8) + EE_START_ADDR + 2;  // Считаем адрес текущей метки
    EEPROM.get(address, buf);               // Читаем метку из памяти
    if (compareUIDs(tag, buf, size)) return address; // Сравниваем - если нашли возвращаем асдрес
  }
  return -1;                                // Если не нашли - вернем минус 1
}

// Удаление или запись новой метки
void saveOrDeleteTag(uint8_t *tag, uint8_t size) {
  int16_t tagAddr = foundTag(tag, size);                      // Ищем метку в базе
  uint16_t newTagAddr = (savedTags * 8) + EE_START_ADDR + 2;  // Адрес крайней метки в EEPROM
  if (tagAddr >= 0) {                                         // Если метка найдена - стираем
    for (uint8_t i = 0; i < 8; i++)  {                        // 8 байт
      EEPROM.write(tagAddr + i, 0x00);                        // Стираем байт старой метки
      EEPROM.write(tagAddr + i, EEPROM.read((newTagAddr - 8) + i)); // На ее место пишем байт последней метки
      EEPROM.write((newTagAddr - 8) + i, 0x00);               // Удаляем байт последней метки
    }
    EEPROM.write(EE_START_ADDR + 1, --savedTags);             // Уменьшаем кол-во меток и пишем в EEPROM
    indicate(DELITED);                                        // Подаем сигнал
  } else if (savedTags < MAX_TAGS) {                          // метка не найдена - нужно записать, и лимит не достигнут
    for (uint16_t i = 0; i < size; i++) EEPROM.write(i + newTagAddr, tag[i]); // Зная адрес пишем новую метку
    EEPROM.write(EE_START_ADDR + 1, ++savedTags);             // Увеличиваем кол-во меток и пишем
    indicate(SAVED);                                          // Подаем сигнал
  } else {                                                    // лимит меток при попытке записи новой
    indicate(DECLINE);                                        // Выдаем отказ
    ledSetup(SUCCESS);
  }
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}
