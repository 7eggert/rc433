#ifndef ARDUINO
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <wiringPi.h>

typedef unsigned char byte;

static const byte pin433 = 4;	// output to sender
static const byte pinLed = 0;	// internal LED
static const byte pin433_Read = 0;       // pin to reveiver; pull low for detect mode
static const byte pin433_ReadEnable = 0; // define to 0 for not including detector code

#else

static const byte pin433 = 4;  // output to sender
static const byte pinLed = 13; // internal LED
static const byte pin433_Read = A0;      // pin to reveiver; pull low for detect mode
static const byte pin433_ReadEnable = 1; // define to 0 for not including detector code

#define usleep(t) delayMicroseconds(t)

#endif

unsigned long hex_in = 0;                // string receiver

static const bool debug_out = false;    // debug output timings?

long total;

static void digitalWriteDelay(byte pin, byte b, unsigned int t)
{
	digitalWrite(pin, b);
	usleep(t);
	if (debug_out)
		total += t;
}

/* preamble: µs of silence
   sync: µs of signal; if set, the bits will be silence-signal
                       instead of signal-silence
   post: µs of extra signal / pause after the bits have beeen transfered
   low1, low2: A 0-bit (low) will be transfered of low1 µs of signal followed by
               low2 µs of pause, while a 1-bit will be low2 µs of signal
               followed by low1 µs of pause. But: See sync
   len: Amount of bits to be transfered
   data: Data to be transfered, lowest bit first
   repetitions: Repeat the signal
 */   
void send433(byte pin, int preamble, int sync, int post, int low1, int low2,
             byte len, unsigned long data, byte repetitions = 4)
{
	/* The sender needs this */
	digitalWrite(pin, 1);
	usleep(10);
	for (byte repetitions_i = repetitions; repetitions_i; repetitions_i--) {
		byte b = 1;
		digitalWriteDelay(pin, 0, preamble);
		total=0;
		if (sync) {
			digitalWriteDelay(pin, 1, sync);
			b = 0;
		}
		byte l = len;
		unsigned long d = data;
		while (l) {
			if (d & 1) {
				digitalWriteDelay(pin, b, low2);	// reverse low2 and low1 for HIGH
				digitalWriteDelay(pin, !b, low1);
			} else {
				digitalWriteDelay(pin, b, low1);
				digitalWriteDelay(pin, !b, low2);
			}
			d >>= 1;
			l--;
		}
		if (post) {
			digitalWriteDelay(pin, b, post);
		}
	}
	digitalWrite(pin, 0);	//turn off sender
}

#ifdef ARDUINO
void setup()
{
	Serial.begin(9600);
	pinMode(pin433, OUTPUT);
	digitalWrite(pin433, 0);	// no jamming
	if (pinLed) {
		pinMode(pinLed, OUTPUT);
		digitalWrite(pinLed, 1);	// on when waiting, pulsed low after send
	}
	pinMode(pin433_ReadEnable, INPUT_PULLUP);
}

void loop() {
  if (pin433_ReadEnable && !digitalRead(pin433_ReadEnable))
    do_detect();

  if (serial_decode()) {
    digitalWrite(pinLed, 0);
    send433(pin433, 12 * 2400, 800,    0, 800, 1600,   12, hex_in, 10);
    //              preamble sync post  signal_zero bits  data
    digitalWrite(pinLed, 1);
    Serial.println(hex_in, HEX);
  }
}


/* detector code  */

static const byte i_max = 128;
static byte R[i_max];
byte i = 0;
byte n = 0;

static unsigned long startmillis;
char hex[9] = "--------";
char hexd[] = "0123456789ABCDEF";

static byte lastr = 1;
void do_detect()
{
	if (pin433_ReadEnable) {
		byte r;
		//  byte r = digitalRead(A0);
		if (lastr)
			r = !(analogRead(pin433_Read) < 400);
		else
			r = (analogRead(pin433_Read) > 500);
		lastr = r;
		if ((i & 1) == r) {	// wait for low on even, high on odd
			if (n > 50)
				goto n255;
			if (n == 255)
				goto n255;
			n++;
		} else if (i < i_max) {
			if (i == 0 && n < 50) {
				n = 0;
			} else {
				if (!i)
					startmillis = millis();
				R[i++] = n;
				n = 0;
			}
		} else
			goto n255;
		// analogRead equals delayMicroseconds(128);
		// delayMicroseconds(100);
		return;

	      n255:
		if (i == 0)	// allow long lead in; r == LOW
			return;
		if (i < i_max)
			R[i++] = n;
		Serial.write("Got data: ");
		Serial.println((int) i);
		Serial.println(startmillis);
		Serial.println(millis());
		Serial.println(millis() - startmillis);
		for (int j = 0; j < i; j++) {
			Serial.print(R[j], DEC);
			Serial.write(" ");
		}
		Serial.write('\n');

		if (i & 1) {
			unsigned long b = 1;
			unsigned long d = 0;
			if (i == 51) {	// this remote uses no sync, but starts bits with high
				i -= 2;
				for (int j = 1; j < i; j += 2) {
					if (R[j] > R[j + 1])
						d |= b;
					b <<= 1;
				}
			} else {
				i -= 1;
				for (int j = 2; j < i; j += 2) {
					if (R[j] > R[j + 1])
						d |= b;
					b <<= 1;
				}
			}
			for (i = 8; i;) {
				--i;
				hex[i] = hexd[d & 0xf];
				d >>= 4;
			}
			Serial.println(hex);
		}
		i = n = 0;
	}
}

bool serial_decode()
{
	static bool received = false;	// received a digit
	static bool clear_in = false;
	if (clear_in) {
		hex_in = 0;
		clear_in = false;
		received = false;
	}
	while (Serial.available()) {
		int c = Serial.read();
		//    char s[] = {'e', 'c', 'h', 'o', ':', ' ', c, ' ', 0};
		switch (c) {
		case '0' ... '9':
			hex_in = (hex_in << 4) + c - '0';
			received = true;
			break;
		case 'a' ... 'f':
			c = c - 'a' + 'A';
			// fallthrough
		case 'A' ... 'F':
			hex_in = (hex_in << 4) + c - 'A' + 10;
			received = true;
			break;
		case 0x0d:
		case 0x0a:
		case ',':
		case ' ':
			return clear_in = received;
		}
		//    Serial.print(s);
		//    Serial.println(c, HEX);
	}
	return false;
}

#else // RasPi

static void on_exit(int sig_nr){
	digitalWrite(pin433, 0);
	exit(0);
}

int main(int argc, char * argv[])
{

	wiringPiSetupSys();
	if (signal(SIGINT, on_exit) == SIG_ERR) {
		printf("\ncan't catch SIGINT\n");
		exit(125);
	}
	if (argc != 3) {
		printf("%s len HEX\n", argv[0]);
		exit(126);
	}
	unsigned long len;    sscanf(argv[1], "%uld", &len   );
	unsigned long hex_in; sscanf(argv[2], "%lx", &hex_in);
	if (debug_out)
		printf("%2.2d %x\n", len, hex_in);
	if (len == 24)
		// hack: I've got exactly one remote with
		// negative encoding and different timimg
		send433(pin433, 12 * 1200, 0, 350, 350,  900, len, hex_in, 4);
	else
		send433(pin433, 12 * 2400, 800, 0, 800, 1600, len, hex_in, 4);
	if (debug_out)
		printf("total %d\n", total);
}

#endif
