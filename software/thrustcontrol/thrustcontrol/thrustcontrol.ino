/*
 Name:		thrustcontrol.ino
 Created:	12/13/2023 6:53:43 PM
 Author:	Julian Wingert
*/

#define VE_LED1            35
#define POWER_LED            36
#define VE_LED2            37  
#define VE_SWITCH1      33
#define VE_SWITCH2      34
#define AI_JOYSTICK_X   A9    

#define MATRIX_KBD0     25
#define MATRIX_KBD1     26
#define MATRIX_KBD2     27
#define MATRIX_KBD3     28
#define MATRIX_KBD4     29
#define MATRIX_KBD5     30
#define MATRIX_KBD6     31
#define MATRIX_KBD7     32

volatile char last_keypad_char = '0';
volatile double joystick_value = 0;

#include <TimerThree.h>
#include <TimerOne.h>

#include <Arduino.h>
#include <U8g2lib.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif

U8G2_SSD1309_128X64_NONAME2_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);  

#include <AccelStepper.h>

// Define a stepper and the pins it will use
AccelStepper stepper(AccelStepper::DRIVER, 4, 5);


#include <Keypad.h>

const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns
//define the cymbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = { 28, 27, 26, 25 }; //connect to the row pinouts of the keypad
byte colPins[COLS] = { 32, 31, 30, 29}; //connect to the column pinouts of the keypad

//initialize an instance of class NewKeypad
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

struct s_control_state {
    volatile bool ve_min_enabled = false;
    volatile bool ve_max_enabled = false;
    volatile long ve_min_position = 0;
    volatile long ve_max_position = 250;
    volatile long position = 0;
    volatile double steps_per_mm = 400;
    volatile double mm_per_second_max = 25;
} control_state;

void run_stepper(void) {
    stepper.runSpeed();
    //stepper.run();
}

void update_display(void) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Pos: ");
    int offset = u8g2.drawStr(32, 10, String(control_state.position / control_state.steps_per_mm).c_str());
    u8g2.drawStr(offset + 33, 10, "mm");

    u8g2.drawStr(0, 24, String(last_keypad_char).c_str());

    offset = u8g2.drawStr(0, 40, String(joystick_value / control_state.steps_per_mm).c_str());
    u8g2.drawStr(offset + 1, 40, "mm");
    
    u8g2.sendBuffer();
}


void setup(void) {
    // set LED Outputs
    pinMode(VE_LED1, OUTPUT);
    pinMode(POWER_LED, OUTPUT);
    pinMode(VE_LED2, OUTPUT);

    pinMode(VE_SWITCH1, INPUT_PULLUP);
    pinMode(VE_SWITCH2, INPUT_PULLUP);

    u8g2.begin();
    u8g2.setFont(u8g2_font_8x13_te);
    u8g2.setFontDirection(0);

    stepper.setAcceleration(300);
    stepper.setMaxSpeed(10000);

    Timer3.initialize(2);
    Timer3.attachInterrupt(run_stepper);

    control_state.position = stepper.currentPosition();
}

double joystick_to_speed(double max_speed) {
    int DEAD_CENTER = 96;
    int raw_jstck = analogRead(AI_JOYSTICK_X);
    int abs_joystick = abs((raw_jstck - 512)) - DEAD_CENTER;
    
    if (abs_joystick < 0) {
        return 0;
    }

    if (raw_jstck > 512) {
        return (max_speed / (511 - DEAD_CENTER)) * abs_joystick;
    }
    else {
        return - (max_speed / (512 - DEAD_CENTER)) * abs_joystick;
    }
}

void loop(void) {
    digitalWrite(VE_LED1, digitalRead(VE_SWITCH1));
    digitalWrite(POWER_LED, digitalRead(VE_SWITCH1));
    digitalWrite(VE_LED2, digitalRead(VE_SWITCH2));
    
    double joystick_normiert = joystick_to_speed(1.0);
    joystick_value = sinh(joystick_normiert) / 1.18 * (control_state.mm_per_second_max * control_state.steps_per_mm);
    stepper.setSpeed(joystick_value);

    control_state.position = stepper.currentPosition();
    update_display();
    //stepper.moveTo(60000);
    char tmp = customKeypad.getKey();
    if (tmp != '\0') {
        last_keypad_char = tmp;
    }
}
