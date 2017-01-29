#include <user_config.h>
#include <SmingCore/SmingCore.h>

// If you want, you can define WiFi settings globally in Eclipse Environment Variables
#ifndef WIFI_SSID
	#define WIFI_SSID "PleaseEnterSSID" // Put you SSID and Password here
	#define WIFI_PWD "PleaseEnterPass"
#endif

// ... and/or MQTT username and password
#ifndef MQTT_USERNAME
	#define MQTT_USERNAME ""
	#define MQTT_PWD ""
#endif

// ... and/or MQTT host and port
#ifndef MQTT_HOST
	#define MQTT_HOST "test.mosquitto.org"
#endif
#ifndef MQTT_PORT
	#ifndef ENABLE_SSL
		#define MQTT_PORT 1883
	#else
		#define MQTT_PORT 8883
	#endif
#endif

#define REL_PIN       12             // GPIO 12 = Red Led and Relay (0 = Off, 1 = On)
#define LED_PIN       13             // GPIO 13 = Green Led (0 = On, 1 = Off)
#define KEY_PIN       0              // GPIO 00 = Button

// Forward declarations
void startMqttClient();
void onMessageReceived(String topic, String message);

Timer procTimer;

// MQTT client
// For quick check you can use: http://www.hivemq.com/demos/websocket-client/ (Connection= test.mosquitto.org:8080)
MqttClient *mqtt;

// Check for MQTT Disconnection
void checkMQTTDisconnect(TcpClient& client, bool flag){
	
	// Called whenever MQTT connection is failed.
	if (flag == true)
		Serial.println("MQTT Broker Disconnected!!");
	else
		Serial.println("MQTT Broker Unreachable!!");
	
	// Restart connection attempt after few seconds
	procTimer.initializeMs(2 * 1000, startMqttClient).start(); // every 2 seconds
}

void onMessageDelivered(uint16_t msgId, int type) {
	Serial.printf("Message with id %d and QoS %d was delivered successfully.", msgId, (type==MQTT_MSG_PUBREC? 2: 1));
}

void pollConnection()
{
	if (mqtt->getConnectionState() != eTCS_Connected)
		startMqttClient(); // Auto reconnect
}

bool relstate = 0;
// Callback for messages, arrived from MQTT server
void onMessageReceived(String topic, String message)
{
	digitalWrite(LED_PIN, 0);
	Serial.print(topic);
	Serial.print(":\r\n\t"); // Pretify alignment for printing
	Serial.println(message);
	if (topic.endsWith("/on")) {
		long val = message.toInt();
		relstate = val;
		digitalWrite(REL_PIN, relstate);
		goto exit;
	}

exit:
	digitalWrite(LED_PIN, 1);
}

String addr;
bool mqttrdy = false;	// FIXME ask the library
void IRAM_ATTR keyHandler()
{
	static bool oldstate = 0;
	bool newstate = digitalRead(KEY_PIN);
	if (newstate == 1 && oldstate == 0) {
		relstate = !relstate;
		digitalWrite(REL_PIN, relstate);
		if (mqttrdy) {
			char msg[2];
			msg[0] = '0' + relstate;
			msg[1] = 0;
			mqtt->publish	(addr + "/controls/relay/on", msg, 1);
		}
	}
}

// Run MQTT client
void startMqttClient()
{
	procTimer.stop();
	String id = WifiAccessPoint.getMAC().substring(8, 4);
	addr = "/devices/" + id;
	if(!mqtt->setWill(addr + "/meta/error","disconnected", 1, true)) {
		debugf("Unable to set the last will and testament. Most probably there is not enough memory on the device.");
	}
	mqtt->connect("Sonoff switch " + id, MQTT_USERNAME, MQTT_PWD, true);
#ifdef ENABLE_SSL
	mqtt->addSslOptions(SSL_SERVER_VERIFY_LATER);

	#include <ssl/private_key.h>
	#include <ssl/cert.h>

	mqtt->setSslClientKeyCert(default_private_key, default_private_key_len,
							  default_certificate, default_certificate_len, NULL, true);

#endif
	mqttrdy = true;
	// Assign a disconnect callback function
	mqtt->setCompleteDelegate(checkMQTTDisconnect);
	mqtt->publish	(addr + "/meta/name", "Sonoff switch", 1);
	mqtt->publish	(addr + "/controls/relay/meta/type", "switch", 1);
	mqtt->subscribe	(addr + "/controls/relay/on");
}

// Will be called when WiFi station was connected to AP
void connectOk()
{
	Serial.println("I'm CONNECTED");
	digitalWrite(LED_PIN, 1);

	// Run MQTT client
	startMqttClient();

	procTimer.initializeMs(20 * 1000, pollConnection).start(); // every 20 seconds
}

// Will be called when WiFi station timeout was reached
void connectFail()
{
	Serial.println("I'm NOT CONNECTED. Need help :(");

	// .. some you code for device configuration ..
}

void init()
{
	Serial.begin(SERIAL_BAUD_RATE); // 115200 by default
	Serial.systemDebugOutput(true); // Debug output to serial

	pinMode(REL_PIN, OUTPUT);
	pinMode(LED_PIN, OUTPUT);
	pinMode(KEY_PIN, INPUT);
	attachInterrupt(KEY_PIN, keyHandler, CHANGE);

	mqtt = new MqttClient(MQTT_HOST, MQTT_PORT, onMessageReceived);

	WifiStation.config(WIFI_SSID, WIFI_PWD);
	WifiStation.enable(true);
	WifiAccessPoint.enable(false);

	// Run our method when station was connected to AP (or not connected)
	WifiStation.waitConnection(connectOk, 20, connectFail); // We recommend 20+ seconds for connection timeout at start
}
