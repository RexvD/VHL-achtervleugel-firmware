#include "DualVNH5019MotorShield.h"

DualVNH5019MotorShield md(7, 8, 9, 12, A1, 7, 8, 10, 12, A1);

const uint8_t pot_pin = A2;
const uint8_t pinA = 2;  // Rotary encoder Pin A
const uint8_t pinB = 3;  // Rotary encoder Pin B

const int8_t kp = -50;
const int8_t ki = -2;
const int8_t kd = -0;

const int16_t start_PWM = 80;                // de motor begint direct te draaien op de startwaarde of langzamer als er minder gas nodig is, daana neemt de smoothing het over.
const uint8_t smoothing_time = 20;           //tijd in millis tussen het verhogen van het PWM van de motor zodat deze rustig versneld. hogere waarde is langzamer versnellen.
const uint8_t amps_poll_interval = 1;        //tijd tussen de metingen van het stroomverbuik.
const uint8_t serial_print_interval = 50;    //tijd tussen de serial prints.
const uint8_t direction_change_delay = 200;  //tijd die de motor om de rem staat wanneer die van richting verandert.
const uint8_t PID_interval = 20;             // iedere 20ms wordt de PID berekend. het veranderen van deze waarde heeft invloed op de I en D hou daar rekening mee.

volatile int encoder_pulsen = 0;
volatile int encoder_pulsen_prev = encoder_pulsen;

uint16_t pot_val = 0;
volatile byte reading;

int16_t smoothing_PWM = 0;  //
int16_t setpoint_PWM = 0;
int16_t PWM = 0;
int16_t last_PWM = 0;
uint32_t last_smoothing;
uint32_t last_smoothing_time = 0;  //
uint32_t last_amps_poll = 0;
uint32_t last_serial_print = 0;
uint32_t last_direction_change = 0;
uint32_t last_PID = 0;
uint32_t timer = 0;  // wordt gelijk gesteld aan millis zodat millis niet elke keer opgevraagd wordt want dat kost veel cpu tijd
uint16_t amps = 0;
uint16_t overcurrent_limit = 0;  // waarde word berekend in loop en is afhankelijk van de PWM
int32_t setpoint_pulsen = 0;
int32_t error = 0;
int32_t previus_error = 0;
int32_t diff_error = 0;
int32_t P = 0;
float I = 0;
float D = 0;
int32_t PID = 0;

bool overcurrent = false;
bool direction_change = false;
bool direction = 0;  // 0= negatief 1=positief
bool previus_direction = direction;
void setup() {
  Serial.begin(2000000);
  Serial.println("Dual VNH5019 Motor Shield");
  md.init();
  pinMode(pot_pin, INPUT);
  pinMode(pinA, INPUT_PULLUP);  // Set Pin_A as input
  pinMode(pinB, INPUT_PULLUP);  // Set Pin_B as input

  // Atach a CHANGE interrupt to PinB and exectute the update function when this change occurs.
  attachInterrupt(digitalPinToInterrupt(pinA), iets, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), anders, CHANGE);
}
void iets() {
  cli();                 //stop interrupts happening before we read pin values
  reading = PIND & 0xC;  // read all eight pin values then strip away all but pinA and pinB's values
  if (reading == B00001100) {
    encoder_pulsen--;  //decrement the encoder's position count
  } else if (reading == B00000000) {
    encoder_pulsen--;
  } else if (reading == B00000100) {
    encoder_pulsen++;
  } else if (reading == B00001000) {
    encoder_pulsen++;
  }
  sei();  //restart interrupts
}

void anders() {
  cli();                 //stop interrupts happening before we read pin values
  reading = PIND & 0xC;  // read all eight pin values then strip away all but pinA and pinB's values
  if (reading == B00001100) {
    encoder_pulsen++;  //increment the encoder's position count
  } else if (reading == B00000000) {
    encoder_pulsen++;
  } else if (reading == B00000100) {
    encoder_pulsen--;
  } else if (reading == B00001000) {
    encoder_pulsen--;
  }
  sei();  //restart interrupts
}

void loop() {
  timer = millis();

  //======================= lees potmeter ==================================

  pot_val = 0.05 * analogRead(pot_pin) + 0.95 * pot_val;
  // setpoint_PWM = map(pot_val, 0, 1023, -400, 400);
  setpoint_pulsen = map(pot_val, 0, 1023, -400, 400);


  //====================== smoothing acceleratie ======================================

  int16_t setpoint_PWM_last_PWM = abs(setpoint_PWM) - abs(last_PWM);

  smoothing_PWM = sqrt(abs(setpoint_PWM_last_PWM));


  if ((setpoint_PWM > start_PWM) && (setpoint_PWM - last_PWM >= smoothing_PWM)) {
    if (last_PWM < start_PWM) {
      PWM = start_PWM;
      last_PWM = start_PWM - smoothing_PWM;
    }
    if (timer - last_smoothing_time >= smoothing_time) {
      PWM = last_PWM + smoothing_PWM;
      last_PWM = PWM;
      last_smoothing_time = timer;
    }

  } else if (setpoint_PWM > 0) {
    PWM = setpoint_PWM;
    last_PWM = PWM;
  }

  if ((setpoint_PWM < -start_PWM) && (setpoint_PWM - last_PWM <= smoothing_PWM)) {
    if (last_PWM > -start_PWM) {
      PWM = -start_PWM;
      last_PWM = -start_PWM + smoothing_PWM;
    }
    if (timer - last_smoothing_time >= smoothing_time) {
      PWM = last_PWM - smoothing_PWM;
      last_PWM = PWM;
      last_smoothing_time = timer;
    }

  } else if (setpoint_PWM < 0) {
    PWM = setpoint_PWM;
    last_PWM = PWM;
  }

  //===================== overcurrent detectie =============================

  if (timer - last_amps_poll > amps_poll_interval) {
    last_amps_poll = timer;
    amps = amps * 0.95 + md.getM2CurrentMilliamps() * 0.05;

    overcurrent_limit = (-3.8137 * PWM * PWM + 2456.2 * abs(PWM) + 159013) * 0.00105 + 600;
    if (amps > overcurrent_limit) {
      overcurrent = true;
      md.setM2Brake(400);
    }
  }

  //=================change direction detectie============================

  if (PWM > 0) {
    direction = 1;  // PWM is groter dan 0 dus positief
  } else {
    direction = 0;  // PWM is kleiner dan 0 dus negatef
  }

  if (direction != previus_direction) {
    direction_change = true;
    previus_direction = direction;

  } else if (timer - last_direction_change >= direction_change_delay) {
    last_direction_change = timer;
    direction_change = false;
  }

  //=============================== PWM naar motor ========================

  if (((PWM < 65) && (PWM > -65)) || (direction_change == true)) {
    md.setM2Brake(400);
    overcurrent = false;
  } else if (overcurrent == false) {
    md.setM2Speed(PWM);
  }

  //==================================== PID compute ===============================

  if (timer - last_PID >= PID_interval) {
    last_PID = timer;

    error = setpoint_pulsen - encoder_pulsen;
    //diff_error = 0.2 * (error - previus_error) + 0.8 * diff_error;
    //previus_error = error;

    P = kp * error;
    if (((abs(P) < 400) && (abs(PID) < 400)) || (direction_change == true)) {  // update de I alleen wanneer de motor nog niet op vol vermogen draait en niet op de rem staat omdat ie van richting verandert.
      I = ki * error + I;
    }
    //D = kd * diff_error;

    I = constrain(I, -100, 100);

    PID = P + I + D;
    PID = constrain(PID, -400, 400);

    setpoint_PWM = PID;
  }

  //============================================================SerialPrints============================================

  if (timer - last_serial_print >= serial_print_interval) {
    last_serial_print = timer;

    P = constrain(P, -400, 400);
    //D = constrain(D, -400, 400);

    Serial.print(PID);
    Serial.print(" - ");
    Serial.print(P);
    Serial.print(" - ");
    Serial.print(I);
    Serial.print(" - ");
    Serial.println(D);



    /*
      //Serial.print(last_PWM);
      //Serial.print(" - ");

      Serial.print(error);
      Serial.print(" - ");
      Serial.print(encoder_pulsen);
      Serial.print(" - ");
      Serial.print(setpoint_PWM);
      Serial.print(" - ");
      Serial.print(PWM);
      Serial.print(" - ");
      Serial.print(amps);
      Serial.print(" - ");
      Serial.print(md.getM2CurrentMilliamps());
      Serial.print(" - ");
      Serial.println(overcurrent_limit);


    */
  }
}
