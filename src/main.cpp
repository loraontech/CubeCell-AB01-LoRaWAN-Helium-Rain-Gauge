//####################### Vincent Huynen #######################/
//################## vincent.huynen@gmail.com ##################/
//######################### AUGUST 2022 ######################/
//#################### CubeCell HELTEC AB01 ####################/
//######################## Version 1.0.0 #######################/

/*
  LoRaWan Rain Gauge
  The tipping bucket rain gauge has a magnetic reed switch that closes momentarily each time the gauge measures 0.011" (0.2794 mm) of rain.
*/

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <CayenneLPP.h>  //con platformio mucho más facil


//ACTIVACIÓN
/* OTAA para*/
//estas claves las sacamos del device de helium
uint8_t devEui[] = {0x60, 0x81, 0xF9, 0x1E, 0x31, 0xFD, 0x5F, 0x5D};
uint8_t appEui[] = {0x60, 0x81, 0xF9, 0xD1, 0xA1, 0x72, 0x80, 0x73};
uint8_t appKey[] = {0x8B, 0xE0, 0x15, 0x58, 0x33, 0x9E, 0x18, 0x16, 0x76, 0x12, 0xCB, 0x07, 0x07, 0xC2, 0xC8, 0x3C};

/* ABP para*/ //esta no la usamos, en principio la otra es mejor
uint8_t nwkSKey[] = {};
uint8_t appSKey[] = {};
uint32_t devAddr = (uint32_t)0x00;

/*LoraWan channelsmask, default channels 0-7*/ //esta la dejo por defecto
uint16_t userChannelsMask[6] = {0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};



// The interrupt pin is attached to GPIO1
#define RAIN_GAUGE_PIN GPIO1

bool wakeUp = false;
int rainGaugeCounter = 0;
int cycleCounter = 0;
int batteryVoltage;
bool ENABLE_SERIAL = false; // Enable serial debug output here if required: si lo dejo en false no pasaría nada

/*LoraWan region, select in arduino IDE tools*/
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

/*LoraWan Class, Class A and Class C are supported*/
DeviceClass_t loraWanClass = LORAWAN_CLASS;

/*the application data transmission duty cycle.  value in [ms].*/
/*For this example, this is the frequency of the device status packets */
uint32_t appTxDutyCycle = 900000; // Default 15 mins=900000
//este será el tiempo entre mensajes cuando haya lluvia
//si no hay lluvia no se manda nada

uint32_t watchDogTimer = 86400000; // Daily health check 24h=86400000
//lo pongo a 3600000 para que sea cada hora
//aunque no se mande nada una vez al día se manda un paquete para comprobar que todo funciona

/*OTAA or ABP*/
bool overTheAirActivation = LORAWAN_NETMODE;

/*ADR enable*/
bool loraWanAdr = LORAWAN_ADR;

/* set LORAWAN_Net_Reserve ON, the node could save the network info to flash, when node reset not need to join again */
bool keepNet = LORAWAN_NET_RESERVE;

/* Indicates if the node is sending confirmed or unconfirmed messages */
bool isTxConfirmed = LORAWAN_UPLINKMODE;

/* Application port */
uint8_t appPort = 1;

/*!
 * Number of trials to transmit the frame, if the LoRaMAC layer did not
 * receive an acknowledgment.
 */
uint8_t confirmedNbTrials = 4;

/* Prepares the payload of the frame */
static void prepareTxFrame()
{
  float vbat = getBatteryVoltage(); 

  CayenneLPP lpp(8); //se crea un objeto de la clase CayenneLPP ¿de tamaño 8?
  lpp.reset(); //se resetea
  lpp.addDigitalInput(15, rainGaugeCounter); //se añade el contador digital de lluvia en el canal 15
  lpp.addAnalogInput(8, vbat / 1000); //se añade la batería analógicamente en el canal 8
  appDataSize = lpp.getSize();
  lpp.copy(appData);

  if (ENABLE_SERIAL) //esto es para debug
  {
    Serial.println();
    Serial.println("Rain gauge counter: " + String(rainGaugeCounter));
    Serial.println("Vbat in mV: " + String(vbat));
    Serial.println("Cycle Counter: " + String(cycleCounter));
    Serial.println("Time: " + String(cycleCounter * appTxDutyCycle));
  }
  rainGaugeCounter = 0;
}

void rainGaugeWakeUp()
{
  detachInterrupt(RAIN_GAUGE_PIN); //desconecta la interrupción?
  rainGaugeCounter++;
  wakeUp = true;
  //!\\ Debounce reed switch
  delay(500);
}

void setup()
{

  if (ENABLE_SERIAL) //debug
  {
    Serial.begin(115200);
  }

  deviceState = DEVICE_STATE_INIT; //siempre empieza en este estado para inicializarse
  LoRaWAN.ifskipjoin(); //if saved net info is OK in lorawan mode, skip join

  wakeUp = false;
  pinMode(RAIN_GAUGE_PIN, INPUT_PULLUP);
  attachInterrupt(RAIN_GAUGE_PIN, rainGaugeWakeUp, FALLING);
}

void loop()
{
  if (wakeUp) //solo vale para debug, lo dice cuando detecta un pulso del pluviómetro (está lloviendo)
  {
    if (ENABLE_SERIAL)
    {
      Serial.println("\nIts (prueba) Raining Men");
    }
  }

  switch (deviceState)
  {
  case DEVICE_STATE_INIT: //inicializar
  {
    printDevParam();
    LoRaWAN.init(loraWanClass, loraWanRegion); //se inicializa
    deviceState = DEVICE_STATE_JOIN; //cambia de estado
    break;
  }
  case DEVICE_STATE_JOIN: //enlazar
  {
    LoRaWAN.join(); //se enlaza
    break;
  }
  case DEVICE_STATE_SEND: //mandar un ciclo
  {
    cycleCounter++; //suma un ciclo a la cuenta de ciclos
    if (rainGaugeCounter > 0 || (cycleCounter * appTxDutyCycle) > watchDogTimer)
    //si hay datos para mandar o ha pasado un día, se manda
    {
      prepareTxFrame(); //llama a este método previamente definido (prepara las variables a mandar)
      if (IsLoRaMacNetworkJoined)
      {
        LoRaWAN.send(); //manda el paquete
      }
      cycleCounter = 0; //vuelve a empezar el ciclo
    }
    deviceState = DEVICE_STATE_CYCLE; //cambia de estado
    break;
  }
  case DEVICE_STATE_CYCLE: //prepara siguiente ciclo
  {
    // Schedule next packet transmission
    txDutyCycleTime = appTxDutyCycle + randr(0, APP_TX_DUTYCYCLE_RND); //como mucho un s más?
    LoRaWAN.cycle(txDutyCycleTime); //un pelin mas de 15min: pone el contador y lo inicializa
    deviceState = DEVICE_STATE_SLEEP; //cambia de estado
    break;
  }
  case DEVICE_STATE_SLEEP: //duerme y gestiona pulsos
  {
    if (wakeUp) //si se despierta es porque ha detectado un pulso del pluviometro
    {
      attachInterrupt(RAIN_GAUGE_PIN, rainGaugeWakeUp, FALLING); //se despierta y lo suma al contador
      wakeUp = false;
    }
    LoRaWAN.sleep(); //se vuelve a dormir
    break;
  }
  default: //en principio nunca entra aquí
  {
    deviceState = DEVICE_STATE_INIT;
    break;
  }
  }
}

// downlink data handling function (from https://github.com/jthiller/)
void downLinkDataHandle(McpsIndication_t *mcpsIndication)
{
  Serial.printf("+REV DATA:%s,RXSIZE %d,PORT %d\r\n", mcpsIndication->RxSlot ? "RXWIN2" : "RXWIN1", mcpsIndication->BufferSize, mcpsIndication->Port);

  switch (mcpsIndication->Port)
  {
  case 1:
    /* Set update interval on port 1 */
    if (ENABLE_SERIAL)
      {
        Serial.println("Setting new update interval.");
        Serial.print("Hours: ");
        Serial.println(mcpsIndication->Buffer[0]);
        Serial.print("Minutes: ");
        Serial.println(mcpsIndication->Buffer[1]);
      }
    // Multiply slot 1 by 1 hr, slot 2 by 1 minute.
    // Values (0-255) submitted in hex.
    // Use a tool like https://v2.cryptii.com/decimal/base64.
    // e.g. `0 10` -> `AAo=` Representing 10 minute interval.
    appTxDutyCycle = (3600000 * mcpsIndication->Buffer[0]) + (60000 * mcpsIndication->Buffer[1]);
    break;
  case 2:
    // For other purposes
    break;
  default:
    break;
  }
}