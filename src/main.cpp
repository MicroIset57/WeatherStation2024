/*
    ESTACION METEOROLOCA
    ISET 57
    Año 2024
*/
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFiMulti.h>
#include <Wire.h>
#include <LiquidCrystal.h>
#include <SPI.h>
#include <SimpleDHT.h>

// Si no está conectado el barometro explota todo!
// #define USAR_BAROMETRO

#ifdef USAR_BAROMETRO
    #include <Adafruit_MPL3115A2.h>
#endif

#ifndef LED_BUILTIN
    #define LED_BUILTIN 2
#endif

#define PIN_BARO_SDA    21  // I2C para el barometro
#define PIN_BARO_SCL    22
#define PIN_DHT11       16  // temperatura y humedad
#define PIN_PLUVIOMETRO 17  // sensor del pluviometro (opto)
#define PIN_VELETA_A    19  // optos de la veleta (son 4 optos)
#define PIN_VELETA_B    18
#define PIN_VELETA_C    5
#define PIN_VELETA_D    4
#define PIN_ANEMOMETRO  23  // opto de velocidad del viento (por interrupcion)
#define PIN_LCD_RS      33  // Display LCD
#define PIN_LCD_EN      25
#define PIN_LCD_D4      26
#define PIN_LCD_D5      27
#define PIN_LCD_D6      14
#define PIN_LCD_D7      32

// La cantidad de pulsos por segundos será proporcional a la velocidad del viento.
#define CONSTANTE_DE_VIENTO 1.0

WiFiClient client;
HTTPClient http;
WiFiMulti wifiMulti;
#ifdef USAR_BAROMETRO
Adafruit_MPL3115A2 baro;
#endif

//               RS, EN, D4, D5, D6, D7
LiquidCrystal lcd(33, 25, 26, 27, 14, 32);
SimpleDHT11 dht11(PIN_DHT11);

// curl.exe -v POST 'http://thingsboard.cloud/api/v1/alumnos2024/telemetry' --header "Content-Type:application/json" --data '{direccion:15}'
String thingsboardAddress = "http://thingsboard.cloud";
String accessToken        = "alumnos2024";
String telemetryEndpoint  = "/api/v1/" + accessToken + "/telemetry";

float rocio          = NAN;  // punto de condensación o de rocio calculada.
float altitud        = NAN;
float lluvia         = NAN;
float presion        = NAN;
float sensacion      = NAN;  // sensacion termica calculada.
float temperaturaMPL = NAN;  // temperatura medida con el MPL3115A2
float temperaturaDHT = NAN;  // temperatura medida con el DHT11
float humedad        = NAN;
float viento         = NAN;  // anemometro
int direccion        = -1;   // rosa de los vientos

// ANEMOMETRO : Centa pulsos por interrupcion
// En cada pulso de la veleta se cuenta un pulso.
// Luego calculamos la velocidad como si fuera un frecuencímetro. (pulsos por segundo)
volatile int ContadorAnemometro = 0;
void IRAM_ATTR contarPulso() { ContadorAnemometro++; }

// Imprime una linea completa, verifica que no exceda el tamaño de 20 chars!
// Si existe el char ° lo reemplaza por el especial.
void printLine(int fila, String txt)
{
    lcd.setCursor(0, fila);

    int len = txt.length();
    if (len < 20)
    {
        // si es mas corta, agrego espacios al final.
        for (int i = 0; i < 20 - len; i++) txt += " ";
    }
    else
    {
        // si es mas larga, la trunco:
        txt = txt.substring(0, 20);
    }

    // imprimo de a uno en el display:
    for (int i = 0; i < 20; i++)
    {
        if (txt[i] == '\xA7')
            lcd.write(byte(0));
        else
            lcd.print(txt[i]);
    }

    txt.replace("\xA7", "°");
    Serial.println(txt);
}

void MostrarEspera(char ch)
{
    for (size_t i = 0; i < 20; i++)
    {
        lcd.setCursor(i, 1);
        lcd.print(ch);
        delay(40);
        digitalWrite(LED_BUILTIN, i % 2);
    }
    for (size_t i = 0; i < 20; i++)
    {
        lcd.setCursor(i, 1);
        lcd.print(' ');
        delay(40);
        digitalWrite(LED_BUILTIN, i % 2);
    }
}

// Inicia la conexión a las redes WiFi
void connect()
{
    printLine(0, "Conectando al wifi..");
    MostrarEspera('.');

    while (wifiMulti.run(10000) != WL_CONNECTED)
    {
        MostrarEspera('.');
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        lcd.clear();
        printLine(0, "WiFi OK !");
        printLine(1, WiFi.SSID().c_str());
    }
    else
    {
        printLine(0, "ERROR EN EL WIFI !!!");
    }
    delay(2000);
}

void LeerValores()
{
    // Sensacion termica:
    // https://es.planetcalc.com/2087/
    // (viento en km/h)
    float vientoExp016 = pow(viento, 0.16);
    sensacion          = 13.12 + 0.6215 * temperaturaMPL - 11.37 * vientoExp016 + 0.3965 * temperaturaMPL * vientoExp016;
    Serial.print("** Sensacion térmica: ");
    Serial.println(sensacion);

    // Punto de condensación o de rocio.
    // https://es.planetcalc.com/248/
    float b       = 237.7;
    float a       = 17.27;
    float aT      = a * temperaturaMPL;
    float bT      = b + temperaturaMPL;
    float humNorm = humedad / 100.0;  // entre 0 y 1
    rocio         = (b * ((aT / bT) + log(humNorm))) / (a - (aT / bT) + log(humNorm));
    Serial.print("** Punto de condensación: ");
    Serial.println(rocio);

    lluvia = random(0, 99) * 1.1;

#ifdef USAR_BAROMETRO
    // sensor de presion y otras yerbas:
    presion        = baro.getPressure();
    altitud        = baro.getAltitude();
    temperaturaMPL = baro.getTemperature();
#endif

    // calculo la velocidad del viento segun el tiempo que tardo entre la lectura actual y la anterior, y la cantidad de pulsos ingresados.
    static unsigned long tiempoAnterior = 0;
    unsigned long tiempoActual          = millis();
    int diff                            = tiempoActual - tiempoAnterior;
    tiempoAnterior                      = tiempoActual;
    if (diff > 0)
    {
        viento = ((float)ContadorAnemometro * CONSTANTE_DE_VIENTO) / (float)diff;
    }
    Serial.print("** ContadorAnemometro: ");
    Serial.println(ContadorAnemometro);
    ContadorAnemometro = 0;

    // leo temepratura del DHT11: esto aplica retraso en la lectura de sensores! ojo!
    byte temperature = 0;
    byte humidity    = 0;
    if (dht11.read(&temperature, &humidity, NULL) != SimpleDHTErrSuccess)
    {
        Serial.println("*** Read DHT11 failed!!");
        temperaturaDHT = NAN;
        humedad        = NAN;
    }
    else
    {
        temperaturaDHT = (float)temperature;
        humedad        = (float)humidity;
    }

    // calculo la veleta segun los pines seteados:
    direccion = digitalRead(PIN_VELETA_A) | (digitalRead(PIN_VELETA_B) << 1) | (digitalRead(PIN_VELETA_C) << 2) | (digitalRead(PIN_VELETA_D) << 3);
}

void SendData()
{
    if (WiFi.status() != WL_CONNECTED) return;
    if (!client.connect("thingsboard.cloud", 80)) return;

    // armo el JSON para el Thingsboard:
    String S = "{";

    // (los valores con error no los envio)
    if (!isnan(altitud))
    {
        S += "\"altitud\":" + String(altitud);
    }
    if (!isnan(rocio))
    {
        if (S.length() > 1) S += ",";
        S += "\"rocio\":" + String(rocio);
    }
    if (!isnan(humedad))
    {
        if (S.length() > 1) S += ",";
        S += "\"humedad\":" + String(humedad);
    }
    if (!isnan(lluvia))
    {
        if (S.length() > 1) S += ",";
        S += "\"lluvia\":" + String(lluvia);
    }
    if (!isnan(presion))
    {
        if (S.length() > 1) S += ",";
        S += "\"presion\":" + String(presion);
    }
    if (!isnan(sensacion))
    {
        if (S.length() > 1) S += ",";
        S += "\"sensacion\":" + String(sensacion);
    }
    if (!isnan(temperaturaMPL))
    {
        if (S.length() > 1) S += ",";
        S += "\"temperatura\":" + String(temperaturaMPL);
    }
    if (!isnan(viento))
    {
        if (S.length() > 1) S += ",";
        S += "\"viento\":" + String(viento);
    }
    if (direccion != -1)
    {
        // paso la direccion a grados entre 0 y 360°.
        float fdir = (360.0 / 16.0) * direccion;
        if (S.length() > 1) S += ",";
        S += "\"direccion\":" + String(fdir);
    }
    S += "}";

    Serial.println("Sending data to ThingsBoard: " + S);

    http.begin(thingsboardAddress + telemetryEndpoint);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(S);
    if (httpCode > 0)
    {
        String response = http.getString();
        Serial.print("Server response: ");
        Serial.println(response);
    }
    else
    {
        Serial.println("HTTP request failed");
    }
    http.end();
}

void ActualizarDisplay()
{
    /*
      Rosa de los vientos:
        1010 - NORTE
        0110 - SUR
        0111 - ESTE
        1011 - OESTE

        1100 - NORESTE
        0101 - SURESTE
        1000 - SUROESTE
        0001 - NOROESTE
    */
    static String rosa[16] = {"  N", "NNW", "  S", "SSE", " NW", "WNW", " SE", "ESE", "  W", "WSW", "  E", "ENE", " SW", "SSW", " NE", "NNE"};

    // +--------------------+
    // |Alt  Pres   Hum Lluv|
    // |30  1016.1  50%  0mm|
    // |Sens Temp   Viento  |
    // | 25°  35°   SUR 13km|
    // +--------------------+

    printLine(0, "Alt  Pres   Hum Lluv");

    String txt;

    txt = (isnan(altitud) ? "0" : String(altitud, 0)) + "  ";
    if (presion < 1000) txt += " ";
    if (presion < 100) txt += " ";
    txt += (isnan(presion) ? "0" : String(presion, 1)) + " ";
    if (humedad < 10) txt += " ";
    txt += (isnan(humedad) ? "0" : String(humedad, 0)) + "% ";
    // if (lluvia < 10) txt += " ";
    txt += (isnan(lluvia) ? "0" : String(lluvia, 0)) + "mm";
    printLine(1, txt);

    printLine(2, "Sens Temp   Viento");

    txt = "";
    if (sensacion < 10) txt += " ";
    txt += (isnan(sensacion) ? "0" : String(sensacion, 0)) + "\xA7  ";
    if (temperaturaMPL < 10) txt += " ";
    txt += isnan(temperaturaMPL) ? "0" : String(temperaturaMPL, 0) + "\xA7  ";
    txt += rosa[direccion] + " ";
    if (viento < 10) txt += " ";
    txt += isnan(viento) ? "0" : String(viento, 0) + "km";
    printLine(3, txt);
}

void setup()
{
    Serial.begin(115200);
    delay(200);

    // configuro pines de sensores (los OPTOS llevan pullup)
    pinMode(PIN_ANEMOMETRO, INPUT);  // este lleva un transistor mas para amplificar la señal porque era muy baja.
    pinMode(PIN_VELETA_A, INPUT_PULLUP);
    pinMode(PIN_VELETA_B, INPUT_PULLUP);
    pinMode(PIN_VELETA_C, INPUT_PULLUP);
    pinMode(PIN_VELETA_D, INPUT_PULLUP);
    pinMode(PIN_PLUVIOMETRO, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);  // Led interno para indicar cosas...
    digitalWrite(LED_BUILTIN, 0);

    // vamos a leer el anemometro mediante interrupciones (solo contamos pulsos)
    attachInterrupt(digitalPinToInterrupt(PIN_ANEMOMETRO), contarPulso, RISING);

// iniciamos el barometro MPL3115A2
#ifdef USAR_BAROMETRO
    baro.begin();
    baro.setSeaPressure(1013.26);  // configuracion para presion en Rosario.
#endif

    // creo el caracter °
    byte Grados[8] = {B11100, B10100, B11100, 0, 0, 0, 0, 0};
    lcd.createChar(0, Grados);

    //-------------------------------------
    // Inicializo el LCD
    lcd.begin(20, 4);
    lcd.noCursor();
    lcd.clear();
    printLine(0, "ESTACION METEOROLOCA");
    printLine(1, "====================");
    printLine(3, "  ISET 57 -  2024   ");
    delay(3000);
    printLine(2, "Compartime esta wifi");
    printLine(3, "ISET57 12345678");
    for (size_t i = 0; i < 7; i++) MostrarEspera('=');
    //-------------------------------------

    // agrego estos wifis para conectarme automaticamente....
    wifiMulti.addAP("Claaaaaro", "johnwick");           // javier
    wifiMulti.addAP("iPhone 15 de Javi", "Efficast1");  // javier
    wifiMulti.addAP("ISET57CLARO", "GONZALO1981");      // iset
    wifiMulti.addAP("ISET57", "12345678");              // otro que hay que compartir

    connect();
}

void loop()
{
    static unsigned long T = 0;
    if (millis() - T > 1000)
    {
        T = millis();  // cada 1 segundo....

        LeerValores();
        ActualizarDisplay();
        SendData();
    }

    // intenta reconectarse cada vez que no este conectado.
    if (WiFi.status() != WL_CONNECTED)
    {
        connect();
    }
}
