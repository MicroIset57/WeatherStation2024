#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFiMulti.h>
#include <Wire.h>
#include <LiquidCrystal.h>

#ifndef LED_BUILTIN
    #define LED_BUILTIN 2
#endif

WiFiClient client;
HTTPClient http;
WiFiMulti wifiMulti;

// curl.exe -v POST 'http://thingsboard.cloud/api/v1/alumnos2024/telemetry' --header "Content-Type:application/json" --data '{direccion:15}'
String thingsboardAddress = "http://thingsboard.cloud";
String accessToken        = "alumnos2024";
String telemetryEndpoint  = "/api/v1/" + accessToken + "/telemetry";

// RS, EN, D4, D5, D6, D7
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

float altitud     = NAN;
float humedad     = NAN;
float lluvia      = NAN;
float presion     = NAN;
float sensacion   = NAN;
float temperatura = NAN;
float viento      = NAN;
int direccion     = -1;

void printAt(int x, int y, const char* txt)
{
    lcd.setCursor(x, y);
    lcd.print(txt);
}

void printAt(int x, int y, float val)
{
    char buf[21];
    // borro unos chars antes de imprimir el valor.
    printAt(x, y, "      ");
    sprintf(buf, "%0.1f", val);
    printAt(x, y, buf);
}

// Inicia la conexión a las redes WiFi
void connect()
{
    int cont = 0;
    printAt(0, 0, "Conectando al wifi..");
    Serial.println("Conectando a las redes WiFi...");

    while (wifiMulti.run(10000) != WL_CONNECTED)
    {
        if (cont++ % 2)
            printAt(0, 0, "Conectando al wifi..");
        else
            printAt(0, 0, "..Conectando al wifi");

        Serial.print(".");
        delay(666);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("");
        Serial.print("Conectado a la red WiFi '");
        Serial.print(WiFi.SSID());
        Serial.print("'   IP address: ");
        Serial.println(WiFi.localIP());

        printAt(0, 0, "Conectado al wifi:  ");
        printAt(0, 1, "                    ");
        printAt(0, 1, WiFi.SSID().c_str());
    }
    else
    {
        printAt(0, 0, "ERROR EN EL WIFI !  ");
        Serial.print("Error en la conexion wifi!");
    }
    delay(2000);
}

void setup()
{
    Serial.begin(115200);
    delay(200);

    lcd.begin(16, 4);
    lcd.clear();
    printAt(0, 0, "ESTACION -----------");
    printAt(0, 1, "------ METEOLOLOGICA");
    printAt(0, 3, " ISET 57       2024 ");
    delay(3000);

    wifiMulti.addAP("Claaaaaro", "johnwick");           // javier
    wifiMulti.addAP("iPhone 15 de Javi", "Efficast1");  // javier
    wifiMulti.addAP("ISET57CLARO", "GONZALO1981");      // iset
    wifiMulti.addAP("ISET57", "12345678");              // otro que hay que compartir

    connect();
}

void LeerValores()
{
    altitud     = random(30, 33);
    humedad     = random(20, 60);
    lluvia      = random(0, 15);
    presion     = random(900, 1250);
    sensacion   = random(20, 26);
    temperatura = random(20, 26);
    viento      = random(0, 15);
    direccion   = (360.0 / 16.0) * random(0, 15);
}

void SendData()
{
    String payload = String("{") +                                     //
                     "\"altitud\":" + String(altitud) + "," +          //
                     "\"humedad\":" + String(humedad) + "," +          //
                     "\"lluvia\":" + String(lluvia) + "," +            //
                     "\"presion\":" + String(presion) + "," +          //
                     "\"sensacion\":" + String(sensacion) + "," +      //
                     "\"temperatura\":" + String(temperatura) + "," +  //
                     "\"viento\":" + String(viento) + "," +            //
                     "\"direccion\":" + String(direccion)              //
                     + "}";

    Serial.println("Sending data to ThingsBoard: " + payload);

    http.begin(thingsboardAddress + telemetryEndpoint);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(payload);
    if (httpCode > 0)
    {
        String response = http.getString();
        Serial.println("Server response: " + response);
    }
    else
    {
        Serial.println("HTTP request failed");
    }
    http.end();
}

void ActualizarDisplay()
{
    // printAt(0, 0, "PRESION 1016.5 kPa  ");
    // printAt(0, 1, "HUM 55%  LLUVIA 0mm ");
    // printAt(0, 2, "TEMP 35°C  SENS 34°C");
    // printAt(0, 3, "VIENTO NE a 33Km/h  ");

    char buf[21];
    lcd.clear();

    sprintf(buf, "PRESION %4.1f kPa ", presion);
    printAt(0, 0, buf);

    sprintf(buf, "HUM %2.0f%% LLUVIA %3.1fmm", humedad, lluvia);
    printAt(0, 1, buf);

    sprintf(buf, "TEMP %2.0f°C  SENS %2.0f°C", temperatura, sensacion);
    printAt(0, 2, buf);

    static String dir[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSO", "SO", "OSO", "O", "ONO", "NO", "NNO"};

    sprintf(buf, "VIENTO %s a %3.1fKm/h", dir[direccion], viento);
    printAt(0, 3, buf);
}

void loop()
{
    static unsigned long T1 = 0;
    if (millis() - T1 > 2500)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            if (client.connect("thingsboard.cloud", 80))
            {
                LeerValores();
                SendData();
                T1 = millis();  // espero 2.5 segs para volver a enviar
            }
        }
    }

    static unsigned long T2 = 0;
    if (millis() - T2 > 1000)
    {
        ActualizarDisplay();
        T2 = millis();  // espero 1 seg para refrescar el LCD
    }

    // intenta reconectarse cada vez que no este conectado.
    if (WiFi.status() != WL_CONNECTED)
    {
        connect();
    }
}
