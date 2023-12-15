/*
 Name:		thrustcontrol.ino
 Created:	12/13/2023 6:53:43 PM
 Author:	Julian Wingert
*/

#define VE_LED1            37
#define POWER_LED            36
#define VE_LED2            35  
#define VE_SWITCH1      34
#define VE_SWITCH2      33
#define AI_JOYSTICK_X   A9    

#define MATRIX_KBD0     25
#define MATRIX_KBD1     26
#define MATRIX_KBD2     27
#define MATRIX_KBD3     28
#define MATRIX_KBD4     29
#define MATRIX_KBD5     30
#define MATRIX_KBD6     31
#define MATRIX_KBD7     32


// statemachine to prevent analog speed on goto and vice versa plus anything going anywhere in menu
#define STATE_STOPPED       0
#define STATE_ANALOG_SPEED  1
#define STATE_GOTO_ENDSTOP  2
#define STATE_IN_SETUP      3

// debouncing is done by counting times pressed is read
#define DEBOUNCE_COUNT      25
#define LONGPRESS_COUNT     500

//physical parameters
#define STEPS_PER_MM                400
#define MAX_SPEED_MM_PER_SECOND     20

// breaking distance for analog move
#define BREAKING_DISTANCE_STEPS     (3 * STEPS_PER_MM)

volatile int state = 0;

volatile char last_keypad_char = ' ';
volatile double joystick_value = 0;
volatile float speed_factor = 1.0;

volatile bool ve_switch1_pressed = false;
volatile bool ve_switch2_pressed = false;
volatile bool ve_switch1_long_pressed = false;
volatile bool ve_switch2_long_pressed = false;

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
    volatile double steps_per_mm = STEPS_PER_MM;
    volatile double mm_per_second_max = MAX_SPEED_MM_PER_SECOND;
} control_state;

void run_stepper(void) {
    switch (state) {
        case STATE_ANALOG_SPEED:
            stepper.runSpeed();
            break;
    
        case STATE_GOTO_ENDSTOP:
            stepper.run();
            break;
    }
}

void update_display(void) {
    int line[5] = { 10, 22, 34, 46, 58 };

    // update LEDs
    digitalWrite(VE_LED1, control_state.ve_min_enabled);
    digitalWrite(VE_LED2, control_state.ve_max_enabled);


    u8g2.clearBuffer();
    //////DEBUG///////
    switch (state) {
    case STATE_STOPPED:
        u8g2.drawStr(0, line[2], "STOPPED");
        break;
    case STATE_ANALOG_SPEED:
        u8g2.drawStr(0, line[2], "ANALOG SPEED");
        break;
    case STATE_GOTO_ENDSTOP:
        u8g2.drawStr(0, line[2], "GOTO ENDSTOP");
        break;
    case STATE_IN_SETUP:
        u8g2.drawStr(0, line[2], "IN SETUP");
        break;
    }
   
    //////DEBUG///////
    int offset = u8g2.drawStr(0, line[0], "Pos: ");
    offset += u8g2.drawStr(offset, line[0], String(control_state.position / control_state.steps_per_mm).c_str());
    u8g2.drawStr(offset, line[0], "mm");

    offset = u8g2.drawStr(0, line[1], "Spd: ");
    if (state == STATE_GOTO_ENDSTOP) {
        offset += u8g2.drawStr(0 + offset, line[1], String((stepper.speed() / control_state.steps_per_mm) * speed_factor).c_str());
    }
    else {
        offset += u8g2.drawStr(0 + offset, line[1], String((joystick_value / control_state.steps_per_mm) * speed_factor).c_str());
    }
    u8g2.drawStr(offset, line[1], "mm/s");

    offset = u8g2.drawStr(0, line[3], "VE0: ");
    if (control_state.ve_min_enabled) {
        offset += u8g2.drawStr(0 + offset, line[3], String(control_state.ve_min_position / control_state.steps_per_mm).c_str());
        u8g2.drawStr(offset, line[3], "mm");
    }
    else {
        u8g2.drawStr(offset, line[3], "-");
    }

    offset = u8g2.drawStr(0, line[4], "VE1: ");
    if (control_state.ve_max_enabled) {
        offset += u8g2.drawStr(0 + offset, line[4], String(control_state.ve_max_position / control_state.steps_per_mm).c_str());
        u8g2.drawStr(offset, line[4], "mm");
    }
    else {
        u8g2.drawStr(offset, line[4], "-");
    }

    u8g2.sendBuffer();
}



void setup(void) {

    Serial.begin(115200);

    // set LED Outputs
    pinMode(VE_LED1, OUTPUT);
    pinMode(POWER_LED, OUTPUT);
    pinMode(VE_LED2, OUTPUT);

    pinMode(VE_SWITCH1, INPUT_PULLUP);
    pinMode(VE_SWITCH2, INPUT_PULLUP);

    u8g2.begin();
    u8g2.setFont(u8g2_font_8x13_te);
    u8g2.setFontDirection(0);

    stepper.setAcceleration(10000);
    stepper.setMaxSpeed(10000);

    Timer3.initialize(2);
    Timer3.attachInterrupt(run_stepper);

    control_state.position = stepper.currentPosition();
}

double joystick_to_speed(void) {
    int DEAD_CENTER = 96;
    int raw_jstck = 1023 - analogRead(AI_JOYSTICK_X);
    int abs_joystick = abs((raw_jstck - 512)) - DEAD_CENTER;
    
    if (abs_joystick < 0) {
        return 0;
    }

    if (raw_jstck > 512) {
        return sinh((1.0 / (511 - DEAD_CENTER)) * abs_joystick) / 1.18 * (control_state.mm_per_second_max * control_state.steps_per_mm);
    }
    else {
        return -sinh((1.0 / (512 - DEAD_CENTER)) * abs_joystick) / 1.18 * (control_state.mm_per_second_max * control_state.steps_per_mm);
    }
}

void check_buttons(void) {
    static unsigned int button_counter_vesw1 = 0;
    static unsigned int button_counter_vesw2 = 0;

    if (!digitalRead(VE_SWITCH1)) {
        button_counter_vesw1++;
        //the equal instead of greater than is to prevent reenabling the button state
        if (button_counter_vesw1 == DEBOUNCE_COUNT) {
            ve_switch1_pressed = true;
        }
        //the equal instead of greater than is to prevent reenabling the button state
        if (button_counter_vesw1 == LONGPRESS_COUNT) {
            ve_switch1_pressed = true;
            ve_switch1_long_pressed = true;
        }
        return;
    }
    else {
        button_counter_vesw1 = 0;
    }

    if (!digitalRead(VE_SWITCH2)) {
        button_counter_vesw2++;
        //the equal instead of greater than is to prevent reenabling the button state
        if (button_counter_vesw2 == DEBOUNCE_COUNT) {
            ve_switch2_pressed = true;
        }
        //the equal instead of greater than is to prevent reenabling the button state
        if (button_counter_vesw2 == LONGPRESS_COUNT) {
            ve_switch2_pressed = true;
            ve_switch2_long_pressed = true;
        }
        return;
    }
    else {
        button_counter_vesw2 = 0;
    }

    if ((ve_switch1_pressed || ve_switch2_pressed)) {
        if (ve_switch1_pressed) {
            ve_switch1_pressed = false;
            if (ve_switch1_long_pressed) {
                ve_switch1_long_pressed = false;
                if (control_state.ve_min_enabled == true) {
                    control_state.ve_min_enabled = false;
                    control_state.ve_min_position = 0;
                }
                return;
            }

            if (control_state.ve_min_enabled == false) {
                control_state.ve_min_enabled = true;
                control_state.ve_min_position = stepper.currentPosition();
            }
            else {
                //pressed on already set endstop, move to this endstop
                //check if stopped
                if (state == STATE_STOPPED && ve_switch1_long_pressed == false) {
                    state = STATE_GOTO_ENDSTOP;
                    stepper.setMaxSpeed(control_state.mm_per_second_max * control_state.steps_per_mm);
                    stepper.moveTo(control_state.ve_min_position);
                }
            }
        }

        if (ve_switch2_pressed) {
            ve_switch2_pressed = false;
            if (ve_switch2_long_pressed) {
                ve_switch2_long_pressed = false;
                if (control_state.ve_max_enabled == true) {
                    control_state.ve_max_enabled = false;
                    control_state.ve_max_position = 0;
                }
                return;
            }
            if (control_state.ve_max_enabled == false) {
                control_state.ve_max_enabled = true;
                control_state.ve_max_position = stepper.currentPosition();
            }
            else {
                //pressed on already set endstop, move to this endstop
                //check if stopped
                if (state == STATE_STOPPED) {
                    state = STATE_GOTO_ENDSTOP;
                    stepper.setMaxSpeed(control_state.mm_per_second_max * control_state.steps_per_mm);
                    stepper.moveTo(control_state.ve_max_position);
                }
            }
        }
    }

}

void run_joystick() {
    joystick_value = joystick_to_speed();
    long motor_position = stepper.currentPosition();
    // check virtual endstop breaking
    speed_factor = 1.0;

    if (joystick_value > 0 && control_state.ve_max_enabled) {
        double distance_to_go = labs(control_state.ve_max_position - motor_position);
        if (distance_to_go < BREAKING_DISTANCE_STEPS) {
            speed_factor = distance_to_go / BREAKING_DISTANCE_STEPS;
        }
    }


    if (joystick_value < 0 && control_state.ve_min_enabled) {
        double distance_to_go = labs(stepper.currentPosition() - control_state.ve_min_position);
        if (distance_to_go < BREAKING_DISTANCE_STEPS) {
            speed_factor = distance_to_go / BREAKING_DISTANCE_STEPS;
        }
    }

    if (joystick_value != 0) {
        state = STATE_ANALOG_SPEED;
        stepper.setSpeed(joystick_value * speed_factor);
    }
    else {
        state = STATE_STOPPED;
    }

}

void loop(void) {

    digitalWrite(POWER_LED, true);
    if (state == STATE_GOTO_ENDSTOP) {
        if (stepper.targetPosition() == stepper.currentPosition()) {
            state = STATE_STOPPED;
        }
    }
    if (state == STATE_STOPPED) {
        // endstop checking, only in stop mode
        check_buttons();
    }

    // analog speed running
    if (state == STATE_STOPPED || state == STATE_ANALOG_SPEED) {
        run_joystick();
    }

    control_state.position = stepper.currentPosition();
    update_display();

    char tmp = customKeypad.getKey();
    if (tmp != '\0') {
        last_keypad_char = tmp;
        switch (last_keypad_char) {
        case '#':
            if (state == STATE_STOPPED) {
                state = STATE_IN_SETUP;
            }
            break;
        case 'A':
        case 'C':
            if (state == STATE_IN_SETUP) {
                state = STATE_STOPPED;
            }
            break;

        }
    }
}
