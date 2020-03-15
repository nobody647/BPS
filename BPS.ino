#define DEBUG_MODE  // comment out this line to disable all serial logging

// clang-format off
#ifdef DEBUG_MODE
#define DEBUG_PRINT(x) Serial.println(x)
#define DEBUG_PRINT_FRAME(x) print_frame(x)
#else
#define DEBUG_PRINT(x)	do {} while (0)
#define DEBUG_PRINT_FRAME(x) do {} while (0)
#endif
// clang-format on

#include "defs.h"

#include <DueTimer.h>  //time0 is used for 60 second precharge period, timer1 is 1hz led blink
#include <due_can.h>

#include <stdint.h>  //includes types i like, luke uint8_t, uint16_t, etc.

#include <algorithm>

// type that represens the state of the program. during charging, program is in
// off state
enum State { OFF, PRECHARGE, READY, ERROR };
volatile State state = OFF;

// if i don't declare these my linter freaks out. does nothing
void __disable_irq();
void __enable_irq();

/**
 *
 * INTERRUPT HANDLERS
 *
 */

// begins the precharge process
// called when switch flipped
void flip_on_handler() {
	noInterrupts();
	DEBUG_PRINT("Precharge switch has been flipped!");
	enter_precharge();
	interrupts();
}

// turns off power and sets off state
// called when switch flipped
void flip_off_handler() {
	noInterrupts();
	DEBUG_PRINT("Precharge switch flipped off!");
	enter_off();
	interrupts();
}

// enters ready state
// called when timer0 expires after 60s of precharge
void precharge_timer_handler() {
	noInterrupts();
	DEBUG_PRINT("Timer0 expired! Attempting to enter ready state");
	enter_ready();
	interrupts();
}

// during the precharge sequence, the precharge LED blinks to let us know that
// it's working
void blink_timer_handler() {
	noInterrupts();
	digitalWrite(PRECHARGE_LED, !digitalRead(PRECHARGE_LED));
	interrupts();
}

void pack_status_handler(CAN_FRAME* frame) {
	noInterrupts();
	DEBUG_PRINT("Received pack frame!");
	DEBUG_PRINT_FRAME(frame);
	const uint16_t current = frame->data.s0;
	if (current > CURRENT_LIMIT) {
		DEBUG_PRINT("Current too high!");
		enter_error();
	}

	// service "watchdog" timer
	Timer2.setPeriod(AGE_LIMIT);
	Timer2.start();
	interrupts();
}

void hilo_volts_handler(CAN_FRAME* frame) {
	noInterrupts();
	DEBUG_PRINT("Received voltage frame!");
	DEBUG_PRINT_FRAME(frame);
	const uint16_t high_value = frame->data.s0;
	if (high_value > HIGH_VOLTAGE_LIMIT) {
		const uint8_t high_module = frame->data.bytes[2];
		DEBUG_PRINT("Voltage too high!");
		DEBUG_PRINT(high_value);
		DEBUG_PRINT(high_module);
		enter_error();
	}

	// data is not aligned with word boundary, we need to do some bit shifting
	const uint16_t low_value = frame->data.bytes[4] |= (frame->data.bytes[3] << 8);
	if (low_value < LOW_VOLTAGE_LIMIT) {
		const uint8_t low_module = frame->data.bytes[5];
		DEBUG_PRINT("Voltage too low!");
		DEBUG_PRINT(low_value);
		DEBUG_PRINT(low_module);
		enter_error();
	}

	// service watchdog timer
	Timer3.setPeriod(AGE_LIMIT);
	Timer3.start();
	interrupts();
}

void temp_dtc_handler(CAN_FRAME* frame) {
	noInterrupts();
	DEBUG_PRINT("Received temp frame!");
	DEBUG_PRINT_FRAME(frame);
	const uint8_t high_temp = frame->data.bytes[0];
	if (high_temp > TEMP_LIMIT) {
		const uint8_t high_module = frame->data.bytes[2];
		DEBUG_PRINT("Temp too high!");
		DEBUG_PRINT(high_temp);
		DEBUG_PRINT(high_module);
		enter_error();
	}

	// service watchdog timer
	Timer4.setPeriod(AGE_LIMIT);
	Timer4.start();
	interrupts();
}

void pack_watchdog_handler() {
	noInterrupts();
	DEBUG_PRINT("Pack watchdog expired! We haven't received any pack data in too long!");
	enter_error();
	interrupts();
}

void voltage_watchdog_handler() {
	noInterrupts();
	DEBUG_PRINT("Voltage watchdog expired! We haven't received any voltage data in too long!");
	enter_error();
	interrupts();
}

void temp_watchdog_handler() {
	noInterrupts();
	DEBUG_PRINT("Temp watchdog expired! We haven't received any temp data in too long!");
	enter_error();
	interrupts();
}

/*
 *
 * STATE CHANGE FUNCTIONS
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

	// starts "watchdog" timers
	Timer2.start();
	Timer3.start();
	Timer4.start();

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

	// close relays
	digitalWrite(RELAY1_LOW_SIDE, HIGH);
	digitalWrite(RELAY2_HIGH_SIDE, HIGH);
	digitalWrite(RELAY3_PRECHARGE, HIGH);
	digitalWrite(RELAY4_MPPT, HIGH);
	DEBUG_PRINT("Relays configured for ON state!");

	// set timers to correct state
	reset_timers();
	digitalWrite(PRECHARGE_LED, HIGH);

	// change program state
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
	Timer0.setPeriod(10000);

	// stop precharge LED blinking if it is
	Timer1.stop();
	digitalWrite(PRECHARGE_LED, LOW);

	// reset age limit watchdog timers
	Timer2.stop();
	Timer.setPeriod(AGE_LIMIT);
	Timer3.stop();
	Timer3.setPeriod(AGE_LIMIT);
	Timer4.stop();
	Timer4.setPeriod(AGE_LIMIT);

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

// stolen from ross
void print_frame(CAN_FRAME* frame) {  // this is good for testing to see if you are getting the
									  // right values before conversion
	Serial.print("ID: 0x");
	Serial.print(frame->id, HEX);
	Serial.print(" Len: ");
	Serial.print(frame->length);
	Serial.print(" Data: 0x");
	for (int count = 0; count < frame->length; count++) {
		Serial.print(frame->data.bytes[count], HEX);
		Serial.print(" ");
	}
	Serial.print("\r\n");
}

void setup() {
	noInterrupts();  // disable interrupts for critical code

	// Configure IO pins
	pinMode(RELAY1_LOW_SIDE, OUTPUT);
	pinMode(RELAY2_HIGH_SIDE, OUTPUT);
	pinMode(RELAY3_PRECHARGE, OUTPUT);
	pinMode(RELAY4_MPPT, OUTPUT);
	pinMode(FAN_CONTROL_PIN, OUTPUT);
	pinMode(PRECHARGE_SWITCH, INPUT_PULLUP);  // pin 11
	pinMode(PRECHARGE_LED, OUTPUT);

 #ifdef DEBUG_MODE
 // Set up serial connection
  Serial.begin(57600);
  Serial.println("Hello world!");
#endif

	DEBUG_PRINT("Pins configured!");

  

	// Configure timers and interrupts
	attachInterrupt(digitalPinToInterrupt(PRECHARGE_SWITCH), flip_on_handler, FALLING);
	attachInterrupt(digitalPinToInterrupt(PRECHARGE_SWITCH), flip_off_handler, RISING);
	Timer0.attachInterrupt(precharge_timer_handler).setPeriod(60000);  // 60 seconds for precharge timer
	Timer1.attachInterrupt(blink_timer_handler).setPeriod(1000);	   // 1 second for led blink
	Timer2.attachInterrupt(pack_watchdog_handler).setPeriod(AGE_LIMIT);
	Timer3.attachInterrupt(voltage_watchdog_handler).setPeriod(AGE_LIMIT);
	Timer4.attachInterrupt(temp_watchdog_handler).setPeriod(AGE_LIMIT);

	DEBUG_PRINT("Interrupts configured!");

	// Begin CAN connection
	Can0.begin(CAN_BAUD);
	Can0.setRXFilter(0, BMS_CAN_BASE | PACK_STATUS, false);
	Can0.setRXFilter(1, BMS_CAN_BASE | HILO_VOLTS, false);
	Can0.setRXFilter(2, BMS_CAN_BASE | TEMP_DTC, false);
	Can0.setCallback(0, pack_status_handler);
	Can0.setCallback(0, hilo_volts_handler);
	Can0.setCallback(0, temp_dtc_handler);

	interrupts();  // enable interrupts

	enter_precharge();
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
