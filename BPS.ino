#define DEBUG_MODE  // comment out this line to disable all serial logging

// clang-format off
#ifdef DEBUG_MODE
#define DEBUG_PRINT(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)	do {} while (0)
#endif
// clang-format on

#include "pin_defs.h"
#include <DueTimer.h>  //time0 is used for 60 second precharge period, timer1 is 1hz led blink
#include <due_can.h>
#include <stdint.h>  //includes types i like, luke uint8_t, uint16_t, etc.

// baud rate used for can. kinda self-explanitory tbh
#define CAN_BAUD 125000

// type that represens the state of the program. during charging, program is in
// off state
enum State { OFF, PRECHARGE, READY, ERROR };
volatile State state = OFF;

// if i don't declare these my linter freaks out. does nothing
void __disable_irq();
void __enable_irq();

// begins the precharge process
// called when switch flipped
void flip_on() {
	noInterrupts();
	DEBUG_PRINT("Precharge switch has been flipped!");
	enter_precharge();
	interrupts();
}

// turns off power and sets off state
// called when switch flipped
void flip_off() {
	noInterrupts();
	DEBUG_PRINT("Precharge switch flipped off!");
	enter_off();
	interrupts();
}

// enters ready state
// called when timer0 expires after 60s of precharge
void timer0_finish() {
	noInterrupts();
	DEBUG_PRINT("Timer0 expired! Attempting to enter ready state");
	enter_ready();
	interrupts();
}

/*
 *
 * state change functions
 *
 */

void enter_precharge() {
	DEBUG_PRINT("Attempting to enter precharge state");
	switch (state) {  // validate call
	case PRECHARGE:   // already in precharge state
		DEBUG_PRINT("https://i.imgur.com/rUAFTOU.jpg");
		return;
	case READY:  // already in ready state
		DEBUG_PRINT("https://i.imgur.com/rUAFTOU.jpg");
		return;
	case ERROR:  // in error state
		DEBUG_PRINT("https://i.imgur.com/rUAFTOU.jpg");
		return;
	}

	digitalWrite(RELAY1_LOW_SIDE, HIGH);
	digitalWrite(RELAY2_HIGH_SIDE, HIGH);
	digitalWrite(RELAY3_PRECHARGE, LOW);
	digitalWrite(RELAY4_MPPT, LOW);
	DEBUG_PRINT("Set relays to PRECHARGE condition!");

	// start countdown to end of precharge sequence
	// when timer0 expires after 1 minute, enter_ready will be called
	Timer0.start();

	Timer1.start();  // start the timer that blinks the precharge led

	state = PRECHARGE;
	DEBUG_PRINT("Timers started and state set to precharge!");
}

// finishes the precharge process, called after 60s of precharge by timer0
void enter_ready() {
	noInterrupts();
	DEBUG_PRINT("Attempting to enter ready state!");
	switch (state) {  // validate call
	case READY:		  // already in ready state
		DEBUG_PRINT("https://i.imgur.com/rUAFTOU.jpg");
		return;
	case OFF:  // bad idea
		DEBUG_PRINT(
			"You're trying to hop directly from off to ready "
			"without precharging! That's a bad idea!");
		DEBUG_PRINT("https://i.imgur.com/rUAFTOU.jpg");
		return;
	case ERROR:  // must turn off, then back on
		DEBUG_PRINT("https://i.imgur.com/rUAFTOU.jpg");
		return;
	}

	digitalWrite(RELAY1_LOW_SIDE, HIGH);
	digitalWrite(RELAY2_HIGH_SIDE, HIGH);
	digitalWrite(RELAY3_PRECHARGE, HIGH);
	digitalWrite(RELAY4_MPPT, HIGH);
	DEBUG_PRINT("Relays configured for ON state!");

	reset_timers();
	digitalWrite(PRECHARGE_LED, HIGH);

	state = READY;
	DEBUG_PRINT("TImers and state configured for ON state!");

	// send a can message
	CAN_FRAME outgoing;
	outgoing.id = 0;  // TODO: change id;
	outgoing.extended = false;
	outgoing.priority = 1;  // 0 is highest priority, 15 is lowest
	Can0.sendFrame(outgoing);
	DEBUG_PRINT("Can frame sent!");

	interrupts();
}

void enter_off() {
	DEBUG_PRINT("Entering off state!");
	isolate_pack();  // switch relays to off position
	reset_timers();

	state = OFF;
	DEBUG_PRINT("Timers and state have been configured for OFF!");
}

void enter_error() {
	DEBUG_PRINT("Entering error state!");
	isolate_pack();  // switch relays to off position
	reset_timers();

	state = ERROR;
	DEBUG_PRINT("Timers and state have been configured for ERROR!");
}

/*
 *
 * utility functions
 *
 */

// reset timers
void reset_timers() {
	// stop countdown to end of precharge sequence if it has been started
	Timer0.stop();
	Timer0.setPeriod(60000);

	// stop precharge LED blinking if it is
	Timer1.stop();
	digitalWrite(PRECHARGE_LED, LOW);

	DEBUG_PRINT("Timers reset!");
}

// open all relays
void isolate_pack() {
	digitalWrite(RELAY1_LOW_SIDE, LOW);
	digitalWrite(RELAY2_HIGH_SIDE, LOW);
	digitalWrite(RELAY3_PRECHARGE, LOW);
	digitalWrite(RELAY4_MPPT, LOW);
	DEBUG_PRINT("Relays have been set to open/disconnected state!");
}

// during the precharge sequence, the precharge LED blinks to let us know that
// it's working
void blink_led() { digitalWrite(PRECHARGE_LED, !digitalRead(PRECHARGE_LED)); }

void setup() {
	noInterrupts();  // disable interrupts for critical code
#ifdef DEBUG
	// Set up serial connection
	Serial.begin(57600);
	Serial.println("Hello world!");
#endif

	// Configure IO pins
	pinMode(RELAY1_LOW_SIDE, OUTPUT);
	pinMode(RELAY2_HIGH_SIDE, OUTPUT);
	pinMode(RELAY3_PRECHARGE, OUTPUT);
	pinMode(RELAY4_MPPT, OUTPUT);
	pinMode(FAN_CONTROL_PIN, OUTPUT);
	pinMode(PRECHARGE_SWITCH, INPUT_PULLUP);  // pin 11
	pinMode(PRECHARGE_LED, OUTPUT);
	DEBUG_PRINT("Pins configured!");

	// Configure timers and interrupts
	attachInterrupt(digitalPinToInterrupt(PRECHARGE_SWITCH), flip_on, FALLING);
	attachInterrupt(digitalPinToInterrupt(PRECHARGE_SWITCH), flip_off, RISING);
	// DueTimer t = DueTimer(2);
	Timer0.attachInterrupt(enter_ready)
		.setPeriod(60000);  // 60 seconds for precharge timer
	Timer1.attachInterrupt(blink_led).setPeriod(
		1000);  // 1 second for led blink
	DEBUG_PRINT("Interrupts configured!");

	// Begin CAN connection
	Can0.begin(CAN_BAUD);
	// TODO: ADD CAN STUFF

	interrupts();  // enable interrupts
}

void loop() {
// allows changing of state over serial for debugging purposes
#ifdef DEBUG_MODE
	String input = "";
	while (Serial.available() > 0) {
		input = Serial.readStringUntil('\n');
		if (input == "off") {
			enter_off();
		} else if (input == "precharge") {
			enter_precharge();
		} else if (input == "ready") {
			enter_ready();
		} else if (input == "error") {
			enter_error();
		}
	}
#endif
}
