#include <RTClib.h>
#include <TimerOne.h>
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>//Se incluye la librería EEPROM

LiquidCrystal_I2C lcd( 0x3f, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE ); //Algunas pantallas llevan por defecto la dirección 27 y otras la 3F

//              DEFINICIÓN PINES ENTRADAS ANALOGICAS
int pinGeneracionFV = 0;     // define el pin 0 como 'Generacion FV'
int pinConsumoGeneral = 1;    // define el pin 1 como 'Consumo General'
int pinTensionCargador = 2;        // define el pin 2 como 'Tension'
int pinConsumoCargador = 3;   // define el pin 3 como 'Consumo Cargador'

//              DEFINICIÓN PINES ENTRADAS DIGITALES
int pinPulsadorInicio = 2;
int pinPulsadorMenos = 3;
int pinPulsadorMas = 4;
int pinPulsadorProg = 5;
int pinAlimentacionCargador = 6;     //Salida que activa el contactor que alimenta el cargador
int pinRegulacionCargador = 9;    //Salida que activa la onda cuadrada

//              DEFINICION TIPOS CARGA
const int TARIFAVALLE = 0;
const int FRAJAHORARIA = 1;
const int INMEDIATA = 2;
const int POTENCIA = 3;
const int FRAJATIEMPO = 4;
const int EXCEDENTESFV = 5;
const int INTELIGENTE = 6;

//              DEFINICION VARIABLES GLOBALES
int horaInicioCarga, minutoInicioCarga, intensidadProgramada, consumoTotalMax, horaFinCarga, minutoFinCarga, generacionMinima, tipoCarga;
bool cargadorEnConsumoGeneral, conSensorGeneral, conFV, inicioCargaActivado;
unsigned long kwTotales;
int duracionPulso, tensionCargador, acumTensionCargador = 0, numTensionAcum = 0, numCiclos = 0;
bool permisoCarga, conectado, cargando, cargaCompleta, cargaIncompleta;
int consumoCargador, generacionFV, consumoGeneral, picoConsumoCargador, picoGeneracionFV, picoConsumoGeneral;
int consumoCargadorAmperios, generacionFVAmperios, consumoGeneralAmperios;

// VARIABLES PARA EL RELOJ RTC ---------------
RTC_DS3231 rtc;

// Define various ADC prescaler--------------
const unsigned char PS_16 = (1 << ADPS2);
const unsigned char PS_32 = (1 << ADPS2) | (1 << ADPS0);
const unsigned char PS_64 = (1 << ADPS2) | (1 << ADPS1);
const unsigned char PS_128 = (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

void setup() {
  //    ---------Se establece el valor del prescaler----------------
  ADCSRA &= ~PS_128;  // remove bits set by Arduino library
  // you can choose a prescaler from above. PS_16, PS_32, PS_64 or PS_128
  ADCSRA |= PS_32;    // set our own prescaler to 32

// DEFINICIÓN DE LOS PINES COMO ENTRADA O SALIDA
  pinMode(pinRegulacionCargador, OUTPUT);
  pinMode(pinAlimentacionCargador, OUTPUT);
  pinMode(pinPulsadorProg, INPUT);
  pinMode(pinPulsadorMas, INPUT);
  pinMode(pinPulsadorMenos, INPUT);
  pinMode(pinPulsadorInicio, INPUT);

  //    ---------   Se recuperan los ultimos valores de las variables guardadas en la eeprom----------------
  horaInicioCarga = EEPROM.read(0);
  minutoInicioCarga = EEPROM.read(1);
  intensidadProgramada = EEPROM.read(2);
  consumoTotalMax = EEPROM.read(3);
  horaFinCarga = EEPROM.read(4);
  minutoFinCarga = EEPROM.read(5);
  cargadorEnConsumoGeneral = EEPROM.read(6);
  conSensorGeneral = EEPROM.read(7);
  generacionMinima = EEPROM.read(8);
  conFV = EEPROM.read(9);
  tipoCarga = EEPROM.read(10);
  kwTotales = EEPROMReadlong(11); // Este dato ocuparia 4 Bytes, por lo que no se pueden usar las direcciones 12, 13 y 14.

  lcd.begin(16, 2);
  lcd.backlight();
  
  Serial.begin(9600);

  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
    while (1);
  }
  
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  if (horaInicioCarga > 24) {    //Si es la primera vez que se ejecuta el programa, la lectura de la Eeprom da un valor alto, así que se asignan valores normales
    horaInicioCarga = 0;
    EEPROM.write(0, horaInicioCarga);
    cargadorEnConsumoGeneral = false;
    EEPROM.write(6, cargadorEnConsumoGeneral); 
    conSensorGeneral = true;
    EEPROM.write(7, conSensorGeneral);
    conFV = false;
    EEPROM.write(9, conFV);
  }
  if (generacionMinima > 100) {
    generacionMinima = 0;
    EEPROM.write(8, generacionMinima);
  }
  if (minutoInicioCarga > 59) {
    minutoInicioCarga = 0;
    EEPROM.write(5, minutoInicioCarga);
  }
  if (intensidadProgramada < 6 ){ // Corregimos el valor de la Intensidad Programada si fuese necesario .... 
    intensidadProgramada = 6;    // no puede ser menor de 6A
    EEPROM.write(2, intensidadProgramada);
  }else if (intensidadProgramada > 32 ){
    intensidadProgramada = 32;   // tampoco puede ser mayor de 32A, ya que es la intensidad máxima que soporta el cargador.
    EEPROM.write(2, intensidadProgramada);
  }
  if (consumoTotalMax > 59) {
    consumoTotalMax = 32;
    EEPROM.write(3, consumoTotalMax); //Si el valor es erroneo lo ponemos a 32
  }
  if (horaFinCarga > 24) {
    horaFinCarga = 0;
    EEPROM.write(4, horaFinCarga);//Si el valor es erroneo lo ponemos a 0
  }
  if (minutoFinCarga > 59) {
    minutoFinCarga = 0;
    EEPROM.write(5, minutoFinCarga); //Si el valor es erroneo lo ponemos a 0
  }
  if (tipoCarga > 3){
    tipoCarga = 0;
    EEPROM.write(10, tipoCarga); //Si el valor es erroneo lo ponemos a 0
  }
  if (kwTotales > 4000000000) {
    kwTotales = 0;
    EEPROM.write(11, 0);//Si el valor es erroneo  reseteamos el valor de los KW acumulados
    EEPROM.write(12, 0);
    EEPROM.write(13, 0);
    EEPROM.write(14, 0);
  }
  
  Timer1.initialize(1000);         // Temporizador que activa un nuevo ciclo de onda
  Timer1.attachInterrupt(RetPulsos); // Activa la interrupcion y la asocia a RetPulsos

  duracionPulso = ((intensidadProgramada * 105 / 7) - 60);
  permisoCarga = false;
  conectado = false;

  lcd.setCursor(0, 0);
  lcd.print("   POLICHARGER  ");
  lcd.setCursor(0, 1);
  lcd.print("**** V4.00 *****");
  delay(1500);
    digitalWrite(pinAlimentacionCargador, HIGH);
}

 //----RUTINA DE GENERACIÓN DE LA ONDA CUADRADA----
void RetPulsos() {                         
  if (permisoCarga) {            // Si hay permiso de carga ......
    if (conectado){               // y además está conectado ......
      digitalWrite(pinAlimentacionCargador, HIGH);    // activamos el pulso ....
      delayMicroseconds(duracionPulso); // durante el tiempo que marca "Duración_Pulsos" .... 
      digitalWrite(pinAlimentacionCargador, LOW);     // desactivamos el pulso ....
    }else{                  // si hay permiso de carga pero no está conectado ....
      digitalWrite(pinAlimentacionCargador, HIGH);    // activamos el pulso ....
    }
  }else{                  // Si no hay permiso de carga ....
    digitalWrite(pinAlimentacionCargador, HIGH);     // activamos el pulso ....
  }
}

void loop() {
  int tension = analogRead(pinTensionCargador);
  int consumoCargadorTemp = analogRead(pinConsumoCargador);  // Leemos el consumo del cargador
  int consumoGeneralTemp = analogRead(pinConsumoGeneral);    // Leemos el consumo general de la vivienda
  int generacionFVTemp = analogRead(pinGeneracionFV);      // Leemos la generación de la instalación fotovoltaica
    
  if (tension > 0){
    acumTensionCargador += tension;
    numTensionAcum++;
  }
  
  if (consumoCargadorTemp > picoConsumoCargador)           // toma el valor más alto de consumo del cargador de entre 300 lecturas
    picoConsumoCargador = consumoCargadorTemp;
  if (consumoGeneralTemp > picoConsumoGeneral)       // toma el valor más alto de consumo general de entre 300 lecturas
    picoConsumoGeneral = consumoGeneralTemp;
  if (generacionFVTemp > picoGeneracionFV)         // toma el valor más alto de generación fotovoltaica de entre 300 lecturas
    picoGeneracionFV = generacionFVTemp;
    
  numCiclos++;
  if (numCiclos > 299){
    numCiclos = 0;
    if (acumTensionCargador > 0 && numTensionAcum > 0){
      tensionCargador = acumTensionCargador / numTensionAcum;
    }
    consumoCargador = picoConsumoCargador;
    consumoGeneral = picoConsumoGeneral;
    generacionFV = picoGeneracionFV;
    acumTensionCargador = 0;
    numTensionAcum = 0;
    picoConsumoCargador = 0;
    picoConsumoGeneral = 0;
    picoGeneracionFV = 0;

    consumoCargadorAmperios = ((consumoCargador - 520) * 4) / 24;    // Calcula el consumo del cargador en Amperios
    if (consumoCargadorAmperios < 0){                   // Se restan 520 porque la lectura se hace a través de un divisor de tensión
      consumoCargadorAmperios = 0;
    }
    consumoGeneralAmperios = ((consumoGeneral - 520) * 4) / 24;     // Calcula el consumo general en Amperios
    if ((consumoGeneralAmperios < 0) || !conSensorGeneral){
      consumoGeneralAmperios = 0;
    }
    generacionFVAmperios = ((generacionFV - 520) * 4) / 24;       // Calcula la generación fotovoltaica en Amperios
    if ((generacionFVAmperios < 0) || !conFV)    {
      generacionFVAmperios = 0;
    }
      
    conectado = (tensionCargador < 660);
    cargando = (tensionCargador < 600);
    DateTime time = rtc.now();
    if (conectado && inicioCargaActivado){
      if (!cargando && !cargaCompleta && !cargaIncompleta){
        bool puedeCargar = false;
        switch(tipoCarga){
          case TARIFAVALLE:
            break;
          case FRAJAHORARIA:
            if (horaInicioCarga > 0 || minutoInicioCarga > 0){
              if (horaInicioCarga >= time.hour() && minutoInicioCarga >= time.minute()){
                puedeCargar = true;
              }
            }
            break;
        
        }
        if (puedeCargar && permisoCarga){
          cargaCompleta = true;
          puedeCargar = false;
        }
        if (puedeCargar)duracionPulso = CalcularDuracionPulso();
        permisoCarga = puedeCargar;
      }else if (cargando){
        if (horaFinCarga == time.hour() && minutoFinCarga == time.minute()){
          permisoCarga = false;
          inicioCargaActivado = false;
          cargaIncompleta = true;
        }
      }
    }else if (!conectado && inicioCargaActivado){
      cargaCompleta = false;
      permisoCarga = false;
      cargaIncompleta = false;
      inicioCargaActivado = false;
    }
  }
}

int CalcularDuracionPulso(){
  int pulso;
  int consumo = ObtenerConsumoRestante();
  if ((consumo < intensidadProgramada) && conSensorGeneral)  {  // Si el consumo restante es menor que la potencia programada y tenemos el sensor general conectado..
    pulso = ((consumo * 100 / 6) - 37);          // calculamos la duración del pulso en función de la intensidad restante
  }else{
    pulso = ((intensidadProgramada * 100 / 6) - 37);
  }
  if (pulso < 72) pulso = 72;   // Si la duración del pulso resultante es menor de 72(6A) lo ponemos a 6 A.
  return pulso;
}

int ObtenerConsumoRestante(){
  if (!conSensorGeneral) {
    return 32;                    // Asignamos 32 Amperios como consumo restante
  }else {
    if (cargadorEnConsumoGeneral) { // Aquí se controla si el cargador está incluido en el trafo de consumo general de la vivienda,
                      // para tenerlo en cuenta al calcular la intensidad disponible.
                      // Esto es debido a que la normativa permite sacar la alimentación del cargador directamente del contador
                      // Se hace en edificios para no tener que tirar la línea desde el CGP de la vivienda hasta el garage          
      return (generacionFVAmperios + consumoTotalMax) - consumoGeneralAmperios ;// Si el trafo que mide consumo general incluye el cargador //no se resta el consumo del cargador
    }else{
      return (generacionFVAmperios + consumoTotalMax) - (consumoGeneralAmperios - consumoCargadorAmperios); // Si el trafo que mide consumo general no incluye el cargador se resta el consumo del cargador
    }
  }
}

void EEPROMWritelong(int address, long value)   //    Función que permite escribir un dato de tipo Long en la eeprom partiendolo en 4 Bytes
{
  //Decomposition from a long to 4 bytes by using bitshift.
  //One = Most significant -> Four = Least significant byte
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

long EEPROMReadlong(long address)       //    Función que permite leer un dato de tipo Long de la eeprom partiendo de 4 Bytes
{
  //Read the 4 bytes from the eeprom memory.
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}
