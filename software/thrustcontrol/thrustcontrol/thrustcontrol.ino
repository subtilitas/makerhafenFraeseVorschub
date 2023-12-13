/*
 Name:		thrustcontrol.ino
 Created:	12/13/2023 6:53:43 PM
 Author:	Julian Wingert
*/
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

struct s_control_state {
    volatile bool ve_min_enabled = false;
    volatile bool ve_max_enabled = false;
    volatile long ve_min_position = 0;
    volatile long ve_max_position = 250;
    volatile long position = 0;
    volatile double steps_per_mm = 66.765;
    volatile double mm_per_second_max = 1000;
} control_state;

void run_stepper(void) {
    //stepper.runSpeed();
    stepper.run();
}

void update_display(void) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Pos: ");
    u8g2.drawStr(32, 10, String(control_state.position / control_state.steps_per_mm).c_str());

    u8g2.sendBuffer();
}


void setup(void) {
    u8g2.begin();
    u8g2.setFont(u8g2_font_8x13_te);
    u8g2.setFontDirection(0);

    stepper.setAcceleration(300);
    stepper.setMaxSpeed(10000);

    Timer3.initialize(2);
    Timer3.attachInterrupt(run_stepper);

    control_state.position = stepper.currentPosition();
}

void loop(void) {
    control_state.position = stepper.currentPosition();
    update_display();
    stepper.moveTo(60000);
}
