/*
 Name:		thrustcontrol.ino
 Created:	12/13/2023 6:53:43 PM
 Author:	Julian Wingert
*/

#define VE_LED1            37
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

//setup state machine
#define MAIN_MENU           0
#define MENU_ENDSTOPS        1
#define MENU_SPEED_ACCEL    2
#define SM_ENDSTOP_0        3
#define SM_ENDSTOP_1        4
#define SM_SPEED            5
#define SM_ACCEL            6
#define MENU_POSITION		7



// debouncing is done by counting times pressed is read
#define DEBOUNCE_COUNT      25

//physical parameters
#define STEPS_PER_MM                7994.84899
#define MAX_SPEED_MM_PER_SECOND     8
#define MAX_ACCEL_MM_PER_SECOND_SQRD     100

// breaking distance for analog move
#define BREAKING_DISTANCE_STEPS     (3 * STEPS_PER_MM)

volatile int state = 0;
volatile int setup_state = 0;

volatile char last_keypad_char = ' ';
volatile double joystick_value = 0;
volatile float speed_factor = 1.0;

volatile bool ve_switch1_pressed = false;
volatile bool ve_switch2_pressed = false;
volatile bool ve_switch1_long_pressed = false;
volatile bool ve_switch2_long_pressed = false;

//display pixel ofsets per line
static int line_offsets[5] = { 10, 22, 34, 46, 58 };

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
byte colPins[COLS] = { 32, 31, 30, 29 }; //connect to the column pinouts of the keypad

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
	volatile double mm_per_second_sqrd_max = MAX_ACCEL_MM_PER_SECOND_SQRD;
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

	// update LEDs
	digitalWrite(VE_LED1, control_state.ve_min_enabled);
	digitalWrite(VE_LED2, control_state.ve_max_enabled);


	u8g2.clearBuffer();
	//////DEBUG///////
	switch (state) {
	case STATE_STOPPED:
	    u8g2.drawStr(0, line_offsets[2], "STOPPED");
	    break;
	case STATE_ANALOG_SPEED:
	    u8g2.drawStr(0, line_offsets[2], "ANALOG SPEED");
	    break;
	case STATE_GOTO_ENDSTOP:
	    u8g2.drawStr(0, line_offsets[2], "GOTO ENDSTOP");
	    break;
	case STATE_IN_SETUP:
	    u8g2.drawStr(0, line_offsets[2], "IN SETUP");
	    break;
	}

	//////DEBUG///////

	int offset = u8g2.drawStr(0, line_offsets[0], "Pos: ");
	offset += u8g2.drawStr(offset, line_offsets[0], String(control_state.position / control_state.steps_per_mm).c_str());
	u8g2.drawStr(offset, line_offsets[0], "mm");

	offset = u8g2.drawStr(0, line_offsets[1], "Spd: ");
	if (state == STATE_GOTO_ENDSTOP) {
		offset += u8g2.drawStr(0 + offset, line_offsets[1], String((stepper.speed() / control_state.steps_per_mm) * speed_factor).c_str());
	}

	if (state == STATE_ANALOG_SPEED) {
		offset += u8g2.drawStr(0 + offset, line_offsets[1], String((joystick_value / control_state.steps_per_mm) * speed_factor).c_str());
	}
	u8g2.drawStr(offset, line_offsets[1], "mm/s");

	offset = u8g2.drawStr(0, line_offsets[3], "VE0: ");
	if (control_state.ve_min_enabled) {
		offset += u8g2.drawStr(0 + offset, line_offsets[3], String(control_state.ve_min_position / control_state.steps_per_mm).c_str());
		u8g2.drawStr(offset, line_offsets[3], "mm");
	}
	else {
		u8g2.drawStr(offset, line_offsets[3], "-");
	}

	offset = u8g2.drawStr(0, line_offsets[4], "VE1: ");
	if (control_state.ve_max_enabled) {
		offset += u8g2.drawStr(0 + offset, line_offsets[4], String(control_state.ve_max_position / control_state.steps_per_mm).c_str());
		u8g2.drawStr(offset, line_offsets[4], "mm");
	}
	else {
		u8g2.drawStr(offset, line_offsets[4], "-");
	}
	u8g2.sendBuffer();
}

void run_setup() {
	float new_speed = 0.0;
	int new_accel = 0;
	static int input_position = 0;
	static char input_buffer[8];
	float new_speed_parsed = 0.0;
	float new_accel_parsed = 0.0;
	int offset = 0;
	//this is running on key release to prevent menu hopping
	//otherwise the pressed key would continue selecting submenus
	char key_pressed = customKeypad.getKey();
	if (key_pressed != '\0') {
		//wait for key release
		while (customKeypad.getKey() != '\0') {
			delay(1);
		}
		//Serial.println(key_pressed);
		//Serial.println(setup_state);
	}

	u8g2.clearBuffer();

	switch (setup_state) {
		//#define MAIN_MENU           0
	case MAIN_MENU:
		u8g2.drawStr(0, line_offsets[0], "SETUP");
		u8g2.drawStr(0, line_offsets[1], "[1] ENDSTOPS");
		u8g2.drawStr(0, line_offsets[2], "[2] SPEEDS/ACC");
		u8g2.drawStr(0, line_offsets[3], "[3] POSITION");
		u8g2.drawStr(0, line_offsets[4], "[#] EXIT SETUP");

		switch (key_pressed) {
		case '1':
			setup_state = MENU_ENDSTOPS;
			break;
		case '2':
			setup_state = MENU_SPEED_ACCEL;
			break;
		case '3':
			setup_state = MENU_POSITION;
			break;
		case '#':
			state = STATE_STOPPED;
			return;
		}
		break;
		//#define ENDSTOP_MENU        1
	case MENU_ENDSTOPS:
		u8g2.drawStr(0, line_offsets[0], "VIRTUAL ENDSTOPS");
		//u8g2.drawStr(0, line_offsets[2], "SET: [1]:VE0 [2]:VE1");
		u8g2.drawStr(0, line_offsets[3], "CLR: [4]:VE0 [5]:VE1");
		u8g2.drawStr(0, line_offsets[4], "[#]: MAIN MENU");

		switch (key_pressed) {
		case '1':
			setup_state = SM_ENDSTOP_0;
			break;
		case '2':
			setup_state = SM_ENDSTOP_1;
			break;
		case '4':
			control_state.ve_min_enabled = false;
			control_state.ve_min_position = 0;
			setup_state = MAIN_MENU;
			state = STATE_STOPPED;
			return;
		case '5':
			control_state.ve_max_enabled = false;
			control_state.ve_max_position = 0;
			setup_state = MAIN_MENU;
			state = STATE_STOPPED;
			return;
		case '#':
			setup_state = MAIN_MENU;
			return;
		}
		break;
		//#define SPEED_MENU          2
	case MENU_SPEED_ACCEL:
		u8g2.drawStr(0, line_offsets[0], "SPEEDS and ACCEL.:");
		u8g2.drawStr(0, line_offsets[2], "[1]SET SPEED");
		u8g2.drawStr(0, line_offsets[3], "[2]SET ACCEL");
		u8g2.drawStr(0, line_offsets[4], "[#] MAIN MENU");

		switch (key_pressed) {
	case '1':
			setup_state = SM_SPEED;
			//clear speed value @ entry
			new_speed = 0.0;
			memset(input_buffer, 0, 8);
			input_position = 0;
			break;
		case '2':
			setup_state = SM_ACCEL;
			break;
		case '#':
			setup_state = MAIN_MENU;
			return;
		}
		//#define SM_ENDSTOP_0        3
	case SM_ENDSTOP_0:
		u8g2.drawStr(0, line_offsets[0], "VIRTUAL ENDSTOP 0:");
		u8g2.drawStr(0, line_offsets[4], "[#] MAIN MENU");

		switch (key_pressed) {
		case '#':
			setup_state = MAIN_MENU;
			return;
		}
		break;
		//#define SM_ENDSTOP_1        4
	case SM_ENDSTOP_1:
		u8g2.drawStr(0, line_offsets[0], "VIRTUAL ENDSTOP 1:");
		u8g2.drawStr(0, line_offsets[4], "[#] MAIN MENU");

		switch (key_pressed) {
		case '#':
			setup_state = MAIN_MENU;
			return;
		}
		break;
		//#define SM_SPEED            5
	case SM_SPEED:
		u8g2.drawStr(0, line_offsets[0], "SET SPEED (<=50mm/s)");

		u8g2.drawStr(0, line_offsets[1], String(String("CUR: ") + String(control_state.mm_per_second_max) + String("mm/s")).c_str());
		u8g2.drawStr(0, line_offsets[2], String(String("NEW: ") + String(input_buffer) + String("mm/s")).c_str());
		u8g2.drawStr(0, line_offsets[3], "[A] ACCEPT [C] CLEAR");
		u8g2.drawStr(0, line_offsets[4], "[#] MAIN MENU");

		switch(key_pressed){
		
		case 'A':
			new_speed_parsed = String(input_buffer).toFloat();
			if (new_speed_parsed > 0.0 && new_speed_parsed <= MAX_SPEED_MM_PER_SECOND) {
				control_state.mm_per_second_max = new_speed_parsed;
			}
			input_position = 0;
			memset(input_buffer, 0, 8);
			break;
		case 'C':
			memset(input_buffer, 0, 8);
			input_position = 0;
			break;
		case '#':
			setup_state = MAIN_MENU;
			return;
		default:
			if (key_pressed != '\0' && input_position < 7 &&
				key_pressed != 'B'&& key_pressed != 'D') {
				if (key_pressed == '*') {
					key_pressed = '.';
				}
				input_buffer[input_position++] = key_pressed;
			}
		}
		break;
		//#define SM_ACCEL      6
	case SM_ACCEL:
		u8g2.drawStr(0, line_offsets[0], String(String("SET ACCEL (<=") + String(MAX_ACCEL_MM_PER_SECOND_SQRD) + String("mms2)")).c_str());

		u8g2.drawStr(0, line_offsets[1], String(String("CUR: ") + String(control_state.mm_per_second_sqrd_max) + String("mms2")).c_str());
		u8g2.drawStr(0, line_offsets[2], String(String("NEW: ") + String(input_buffer) + String("mms2")).c_str());
		u8g2.drawStr(0, line_offsets[3], "[A] ACCEPT [C] CLEAR");
		u8g2.drawStr(0, line_offsets[4], "[#] MAIN MENU");

		switch (key_pressed) {

		case 'A':
			new_accel_parsed = String(input_buffer).toInt();
			//Serial.println(input_buffer);
			if (new_accel_parsed > 0 && new_accel_parsed <= MAX_ACCEL_MM_PER_SECOND_SQRD) {
				control_state.mm_per_second_sqrd_max = new_accel_parsed;
				stepper.setAcceleration(new_accel_parsed* STEPS_PER_MM);
			}
			input_position = 0;
			memset(input_buffer, 0, 8);
			break;
		case 'C':
			memset(input_buffer, 0, 8);
			input_position = 0;
			break;
		case '#':
			setup_state = MAIN_MENU;
			return;
		default:
			if (key_pressed != '\0' && input_position < 7 &&
				key_pressed != 'B' && key_pressed != 'D' && key_pressed != '*') {
				input_buffer[input_position++] = key_pressed;
			}
		}
		break;
		//#define SM_SET_POSITION     7
	case MENU_POSITION:
		u8g2.drawStr(0, line_offsets[0], "CLEAR POSITION:");
		offset = u8g2.drawStr(0, line_offsets[1], "Pos: ");
		offset += u8g2.drawStr(offset, line_offsets[1], String(control_state.position / control_state.steps_per_mm).c_str());
		u8g2.drawStr(offset, line_offsets[1], "mm");
		u8g2.drawStr(0, line_offsets[2], "[C] CLR POS (0.00)");
		u8g2.drawStr(0, line_offsets[4], "[#] MAIN MENU");
		
		switch (key_pressed) {
		case '#':
			setup_state = MAIN_MENU;
			return;
		case 'C':
			control_state.position = 0;
			stepper.setCurrentPosition(0);
			Serial.println("key_pressed");
			break;
		}
		break;

	}
	

	u8g2.sendBuffer();
}





void setup(void) {

	Serial.begin(115200);

	// set LED Outputs
	pinMode(VE_LED1, OUTPUT);
	pinMode(VE_LED2, OUTPUT);

	pinMode(VE_SWITCH1, INPUT_PULLUP);
	pinMode(VE_SWITCH2, INPUT_PULLUP);

	u8g2.begin();
	u8g2.setFont(u8g2_font_6x12_te);
	u8g2.setFontDirection(0);

	stepper.setAcceleration(MAX_ACCEL_MM_PER_SECOND_SQRD * STEPS_PER_MM);
	stepper.setMaxSpeed(STEPS_PER_MM * MAX_SPEED_MM_PER_SECOND);

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
		return;
	}
	else {
		button_counter_vesw2 = 0;
	}

	if ((ve_switch1_pressed || ve_switch2_pressed)) {
		if (ve_switch1_pressed) {
			ve_switch1_pressed = false;

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
	//only fetch keys when NOT in setup
	if (state == STATE_STOPPED && state != STATE_IN_SETUP) {
		//the getkey function clears the set key, so it has to be stored in last_keypad_char to be available furthermore
		char tmp = customKeypad.getKey();
		if (tmp != '\0') {
			last_keypad_char = tmp;
			//enter setup menu on #
			if (state == STATE_STOPPED && tmp == '#') {
				state = STATE_IN_SETUP;
			}
		}
	}
	control_state.position = stepper.currentPosition();
	if (state != STATE_IN_SETUP) {
		update_display();
	}

	if (state == STATE_IN_SETUP) {
		run_setup();
	}

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

}
