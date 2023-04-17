#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <U8g2lib.h>
#include <Wire.h>

//RFID
#include <SPI.h>
#include <MFRC522.h>
// Fin RFID

#include "Credentials.h"

// Almacenamiento de datos permanentes
#include <Preferences.h>
Preferences preferences;

#define LED 2
#define LEDWIFI 0
#define LEDFIREBASE 4

/*#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
*/

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"


#include "time.h"
//const long gmtOffset_sec = 3600; // UTC: Epaña UTC + 1
//const int daylightOffset_sec = 3600;
const long gmtOffset_sec = 0; // UTC: Epaña UTC + 1
const int daylightOffset_sec = 0;


// Define Firebase objects
FirebaseData stream;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
// Variable to save USER UID
String uid;

// Variables to save database paths
String databasePath;
String tempPath;
String humPath;
String presPath;
String listenerPath;

String	firestorePathTags;

// Sensor
float temperature;
float humidity;
float pressure;


// Timer variables (send new readings every three minutes)
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 180000;


// Declare outputs
const int output1 = 2; // 2
const int output2 = 4; // 12
// Variable to save input message
String message;

String ubicacionESP;

// RFID
#define RST_PIN       15          // Configurable, see typical pin layout above
#define SS_PIN        5         // Configurable, take a unused pin, only HIGH/LOW required, must be different to SS 2
#define NR_OF_READERS   1

MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class

MFRC522::MIFARE_Key key; 
// Init array that will store new NUID 
byte nuidPICC[4];

String tarjetaRFIDLeida;


// Tiempo Timestamp
int timestamp;
const char* ntpServer = "pool.ntp.org";
String sTimestamp;
char cTimestamp[20];

void initRFID(){
	Serial.println(F("Initialize RFID System"));

	SPI.begin();
	rfid.PCD_Init();

	Serial.print(F("Reader :"));
	rfid.PCD_DumpVersionToSerial();
}

U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
const char COPYRIGHT_SYMBOL[] = {0xa9, '\0'};
void u8g2_prepare() {
	u8g2.setFont(u8g2_font_6x10_tf);
	u8g2.setFontRefHeightExtendedText();
	u8g2.setDrawColor(1);
	u8g2.setFontPosTop();
	u8g2.setFontDirection(0);
}

// Initialize WiFi
void initWiFi() {
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
	Serial.print("Connecting to WiFi ..");
	while (WiFi.status() != WL_CONNECTED) {
		Serial.print('.');
		delay(1000);
	}
	digitalWrite(LEDWIFI, HIGH);
	Serial.println(WiFi.localIP());
	Serial.println();
}


// Function that gets current epoch time
unsigned long getTime() {
  char fecha[11];
  char hora[9];

  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return(0);
  }

  // "2014-10-02T15:01:23Z"); // RFC3339 UTC "Zulu" format

  strftime(fecha, 11, "%F", &timeinfo); //  YYYY-MM-DD  https://cplusplus.com/reference/ctime/strftime/
  strftime(hora, 9, "%T", &timeinfo); // HH:MM:SS
  //sTimestamp = fecha + "T" + hora + "Z";

  	sTimestamp = String(fecha) + "T" + String(hora) + "Z";
//	strcat(cTimestamp, "T");
//	strcat(cTimestamp, hora);
//	strcat(cTimestamp, "Z");

	Serial.println();
	Serial.print(F("Fecha: "));
	Serial.println(sTimestamp.c_str());

  time(&now);
  return now;
}

// Escribe el texto pasado en la pantalla OLED
void escribeTextoOled(String texto){
	u8g2.clearBuffer();
	u8g2_prepare();
	u8g2.drawStr(0, 0, texto.c_str());
	u8g2.sendBuffer();
}

void firestoreDataUpdate(String tag){
	firestorePathTags = "tags/" + tag;

	FirebaseJson	contenido;

	contenido.set("fields/cod_tag/stringValue", tag.c_str());
	contenido.set("fields/esta_activo/booleanValue", false);
	contenido.set("fields/fec_ini_activo/nullValue");
	contenido.set("fields/fec_fin_activo/nullValue");
	
	if(Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", firestorePathTags.c_str(), contenido.raw())){
		Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
		return;
	}else{
		Serial.println(fbdo.errorReason());
	}

}

void firestoreWriteLog(String tag){
	firestorePathTags = "logs/" + String(timestamp);


	FirebaseJson	contenido;

	contenido.set("fields/cod_tag/stringValue", tag.c_str());
	contenido.set("fields/permitido/booleanValue", false);
	contenido.set("fields/timestamp/timestampValue", String(sTimestamp));
//	contenido.set("fields/timestamp/stringValue", String(timestamp));
	contenido.set("fields/esp/stringValue", ubicacionESP);
	
	if(Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", firestorePathTags.c_str(), contenido.raw())){
		Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
		return;
	}else{
		Serial.println(fbdo.errorReason());
	}

}
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
	digitalWrite(LEDWIFI, LOW);
	Serial.println("Disconnected from WiFi access point");
	Serial.print("WiFi lost connection. Reason: ");
	Serial.println(info.wifi_sta_disconnected.reason);

	Serial.println("Reboot");
	// Restart ESP
	ESP.restart();

	//initWiFi();
}


// Write float values to the database
void sendFloat(String path, float value){
	if (Firebase.RTDB.setFloat(&fbdo, path.c_str(), value)){
		Serial.print("Writing value: ");
		Serial.print (value);
		Serial.print(" on the following path: ");
		Serial.println(path);
		Serial.println("PASSED");
		Serial.println("PATH: " + fbdo.dataPath());
		Serial.println("TYPE: " + fbdo.dataType());
	}
	else {
		Serial.println("FAILED");
		Serial.println("REASON: " + fbdo.errorReason());
	}
}


// Callback function that runs on database changes
void streamCallback(FirebaseStream data){
	Serial.printf("Stream Path, %s\nEvent Path, %s\nData Type, %s\nEvent Type, %s\n\n",
		data.streamPath().c_str(),
		data.dataPath().c_str(),
		data.dataType().c_str(),
		data.eventType().c_str());
	
	printResult(data); //see addons/RTDBHelper.h
	Serial.println();


	// Get the path that triggered the function
	String streamPath = String(data.dataPath());


/* When it first runs, it is triggered on the root (/) path and returns a JSON with all key
	 and values of that path.So, we can get all values from the database and updated the GPIO
	 states, PWM, and message on OLED*/

	if (data.dataTypeEnum() == fb_esp_rtdb_data_type_json){
		FirebaseJson *json = data.to<FirebaseJson *>();
		FirebaseJsonData result;


		if (json->get(result, "/message", false)){
			String message = result.to<String>();
			Serial.println(message);
//      displayMessage(message);


			escribeTextoOled(message);
//      u8g2.clearBuffer();
//      u8g2_prepare();
//      u8g2.drawStr(0, 0, message.c_str());
//      u8g2.sendBuffer();
		}
		if (json->get(result, "/leds/" + String(output1), false)){
			bool state = result.to<bool>();
			digitalWrite(output1, state);
		}
		if (json->get(result, "/leds/" + String(output2), false)){
			bool state = result.to<bool>();
			digitalWrite(output2, state);
		}
	}


	// Check for changes in the digital output values
	if(streamPath.indexOf("/leds/") >= 0){
		// Get string path lengh
		int stringLen = streamPath.length();

		// Get the index of the last slash
		int lastSlash = streamPath.lastIndexOf("/");

		// Get the GPIO number (it's after the last slash "/")
		// UsersData/<uid>/outputs/digital/<gpio_number>
		String gpio = streamPath.substring(lastSlash+1, stringLen);
		Serial.print("DIGITAL GPIO: ");
		Serial.println(gpio);

		// Get the data published on the stream path (it's the GPIO state)
		if(data.dataType() == "int") {
			int gpioState = data.intData();
			Serial.print("VALUE: ");
			Serial.println(gpioState);
			//Update GPIO state
			digitalWrite(gpio.toInt(), gpioState);
		}
		Serial.println();
	}
		// Check for changes in the message
	else if (streamPath.indexOf("/message") >= 0){
		if (data.dataType() == "string") {
			message = data.stringData();
			Serial.print("MESSAGE: ");
			Serial.println(message);
			// Print on OLED
			// displayMessage(message);
			escribeTextoOled(message);
		}
	}

	// This is the size of stream payload received (current and max value)
	// Max payload size is the payload size under the stream reconnection takes place.
	// This max value will be zero as no payload received in case of ESP8266 which
	// BearSSL reserved Rx buffer size is less that the actual stream payload.

	Serial.printf("Received stream payload size: %d (Max. %d)\n\n",
		data.payloadLength(),
		data.maxPayloadLength());

/*  digitalWrite(LEDFIREBASE, HIGH);
	delay(100);
	digitalWrite(LEDFIREBASE, LOW);
*/

}

void streamTimeoutCallback(bool timeout){
	if (timeout)
		Serial.println("Stream timeaut, resuming...\n");

	if (!stream.httpConnected())
		Serial.printf("Error code: %d, reason: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
}


void obtenerUbicacionESP(){
	preferences.begin("dispositivo", false);
	ubicacionESP = preferences.getString("ubicacion", "null");

	if (ubicacionESP == "null"){
		int cont = 0;
		String pathUbicacion = databasePath + "/ubicacion";
		// buscar la ubicación en Firebase
		Serial.println("BUSCANDO UBICACION en Firebase:");
		Serial.println(pathUbicacion);

		while (cont <=60 && ubicacionESP != "null"){
			// Se consulta en Firebase RTDB si tiene nombre

			if (Firebase.RTDB.getString(&fbdo, pathUbicacion.c_str())){
				Serial.println("UBICACION: ");
				ubicacionESP = fbdo.to<const char *>();
				//Serial.println(fbdo.to<const char *>());
				Serial.println(ubicacionESP);
				preferences.putString("ubicacion", ubicacionESP);
			}
			else{
				Serial.println("FAILED");
				Serial.println("REASON: " + fbdo.errorReason());
			}

			delay(5000); // Se espera 5 segs antes de la siguiente petición
			cont++;
		}
		// Si se sale y aún no tiene nombre, se resetea el dispositivo
		if (ubicacionESP == "null"){
			ESP.restart();
		}else{
			// Se guarda la ubicación en memoria permanente y se utiliza en las rutas de Firebase
			Serial.println("******* UBICACION OBTENIDA ********");
		}
	}
	else{
		Serial.println("La Ubicacion es: ");
		Serial.println(ubicacionESP);
	}

}

void obtenerRFIDautorizadas(){
	Serial.println("Obtener las RFIDs autorizadas");
}

void setup() {

	u8g2.begin();
	u8g2_prepare();

	// put your setup code here, to run once:
	Serial.begin(115200);
	pinMode(LED, OUTPUT);
	pinMode(LEDWIFI, OUTPUT);
	pinMode(LEDFIREBASE, OUTPUT);

	// Inicio RFID
	initRFID();

	// Initialize WiFi
	initWiFi();

	WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

	// Para configurar el TimeStamp
	configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


	// Assign the api key (required)
	config.api_key = API_KEY;
	// Assign the user sign in credentials
	auth.user.email = USER_EMAIL;
	auth.user.password = USER_PASSWORD;

	// Assign the RTDB URL (required)
	config.database_url = DATABASE_URL;

	Firebase.reconnectWiFi(true);
	fbdo.setResponseSize(4096);
	// Assign the callback function for the long running token generation task */
	config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
	// Assign the maximum retry of token generation
	config.max_token_generation_retry = 5;
	// Initialize the library with the Firebase authen and config
	Firebase.begin(&config, &auth);
	// Getting the user UID might take a few seconds
	Serial.println("Getting User UID");
	while ((auth.token.uid) == "") {
		Serial.print('.');
		delay(1000);
	}
	// Print user UID
	uid = auth.token.uid.c_str();
	Serial.print("User UID: ");
	Serial.print(uid);


	// Update database path
	databasePath = "/UsersData/" + uid;
	// Define database path for sensor readings
	// --> UsersData/<user_uid>/sensor/temperature
	tempPath = databasePath + "/sensor/temperature";
	// --> UsersData/<user_uid>/sensor/humidity
	humPath = databasePath + "/sensor/humidity";

	// Update database path for listener
	listenerPath = databasePath + "/outputs/";

	


	// Obtiene donde está ubicado el dispositivo.
	// Si no tiene, no avanza hasta que en la RTDB se indica donde está

	obtenerUbicacionESP();
	obtenerRFIDautorizadas();


	// Streaming (whenever data changes on a path)
	// Begin stream on a database path --> UserData/<user_id>/outputs
	if (!Firebase.RTDB.beginStream(&stream, listenerPath.c_str()))
		Serial.printf("Stream begin error, %s\n\n", stream.errorReason().c_str());

	// Assign a calback function to run when it detects changes on the database
	Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);

}

void printDec(byte *buffer, byte bufferSize) {
	String tarjetaRFID;

	for (byte i = 0; i < bufferSize; i++) {
		Serial.print(buffer[i] < 0x10 ? " 0" : " ");
		Serial.print(buffer[i], DEC);

		tarjetaRFID += buffer[i];
	}

	Serial.println();
	Serial.print(tarjetaRFID);
}

void printHex(byte *buffer, byte bufferSize) {
	tarjetaRFIDLeida = "";

	for (byte i = 0; i < bufferSize; i++) {
		Serial.print(buffer[i] < 0x10 ? " 0" : " ");
		Serial.print(buffer[i], HEX);

		tarjetaRFIDLeida += String(buffer[i], HEX);
	}

	for (int i = 0; i < tarjetaRFIDLeida.length(); i++) {tarjetaRFIDLeida[i] = toupper(tarjetaRFIDLeida[i]);}

	Serial.println();
	Serial.print(tarjetaRFIDLeida);
}

void readRFID(){
	// Look for new 1 cards
	if ( ! rfid.PICC_IsNewCardPresent())
		return;

	// Verify if the NUID has been readed
	if (  !rfid.PICC_ReadCardSerial())
		return;

	// Se comprueba el tipo de tarjeta RFID
	Serial.print(F("PICC type: "));
	MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
	Serial.println(rfid.PICC_GetTypeName(piccType));


		// Store NUID into nuidPICC array
	for (byte i = 0; i < 4; i++) {
		nuidPICC[i] = rfid.uid.uidByte[i];
	}

	Serial.print(F("RFID In dec: "));
	printDec(rfid.uid.uidByte, rfid.uid.size);
	Serial.println();
	printHex(rfid.uid.uidByte, rfid.uid.size);
	Serial.println();


    //Get current timestamp
    timestamp = getTime();


	escribeTextoOled(tarjetaRFIDLeida);
	firestoreWriteLog(tarjetaRFIDLeida);
	firestoreDataUpdate(tarjetaRFIDLeida);

	// Halt PICC
	rfid.PICC_HaltA();

	// Stop encryption on PCD
	rfid.PCD_StopCrypto1();
}

void loop() {

	readRFID();

	// put your main code here, to run repeatedly:
	// Se comenta para que se encienda desde FireBase
	/*digitalWrite(LED, HIGH);
	delay(1000);
	digitalWrite(LED, LOW);
	delay(1000);
	*/

/* DHT*/
/*  float h = dht.readHumidity();
	float t = dht.readTemperature();

	if (isnan(h) || isnan(t)) {
		Serial.println(F("Failed to read from DHT sensor!"));
		return;
	}

	Serial.print(F("Humedad: "));
	Serial.print(h);
	Serial.print(F("% Temperatura: "));
	Serial.print(t);
	Serial.println(F("°C "));
*/

	// Send new readings to database
	if (Firebase.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0)){
		sendDataPrevMillis = millis();

/* DHT */
/*
		// Get latest sensor readings
		temperature = dht.readTemperature();
		humidity = dht.readHumidity();

		// Send readings to database:
		sendFloat(tempPath, temperature);
		sendFloat(humPath, humidity);
*/
	}
}
