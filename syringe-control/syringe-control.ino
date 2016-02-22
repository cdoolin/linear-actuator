/*
 *  Syring pump control firmware.
 * 
 *  Copyright 2016 Callum Doolin (doolin@ulaberta.ca) 
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */


const byte NMOTORS = 2;

const unsigned int DIRS[] = {A1, A3};
const unsigned int STEPS[] = {A0, A2};

bool go[NMOTORS];
unsigned int count[NMOTORS];
unsigned int target[NMOTORS];



inline void timer1_stop()
{
    TCCR1B = 0;
    TCCR1A = 0;
}

inline void timer1_start()
{
    // start timer
    TCCR1A = _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | // fast pwm.  use ICR1 as timer top.
             _BV(CS11);  // 8^(2 - 1) = 8x prescaler (1/2 us per tick)
}


void setup()
{
    // configure motor pins
    for (int i = 0; i < NMOTORS; i++) {
        pinMode(DIRS[i], OUTPUT);
        digitalWrite(DIRS[i], LOW);
        pinMode(STEPS[i], OUTPUT);
        digitalWrite(STEPS[i], LOW);

        go[i] = false;
        count[i] = 0;
        target[i] = 100;
    }

    // start serial
    Serial.begin(9600);
    Serial.setTimeout(1000);
    while (!Serial) {}

    // configure timer.
    timer1_stop();
    
    ICR1 = 200; // user ICR1 for top of counter,  set it.  interrupt every 50us (360 clock cycles).
    TCNT1 = 0; // reset timer counter
    TIMSK1 = _BV(TOIE1);  // Timer Overflow Interrupt Enabled

    timer1_start();
}

inline void step(unsigned int motor)
{
    digitalWrite(STEPS[motor], HIGH);
    digitalWrite(STEPS[motor], LOW);
}


// reads the next part of a message from the serial port.
// if it's the end of a word (' ' terminated), returns with 1
// if it's the end of the message ('\n' terminated),  returns with 0
// if the message is terminated improperly (eg. timeout) returns with -1
bool echo = true;

int read_next(String &out, const int MAXBYTES=32)
{
    char buff[MAXBYTES + 1];
    char *p = buff;
    int readed = 0;
    
    while (1) {
        readed = Serial.readBytes(p, 1);

        if (readed == 0) // timeouted, end message.
            return -1;

        if (p > buff + MAXBYTES - 1) // we're gonna overflow!
            return -1;
            
        if ((*p == '\n' || *p == '\r')) {
            if (echo) Serial.write("\r\n");
            *p = 0;
            out = buff;
            return 0; // end message
        }
        
        if (*p == ' ' && p > buff) {
            if (echo) Serial.write(*p);
            *p = 0;    
            out = buff;  
            return 1; 
        }
        
        if (*p > 32 && *p < 127) { 
            // got a printable character
            if (echo) Serial.write(*p);
           
            p++;
        }
    }
}

byte read_message(String *words, byte MAXWORDS=4)
{
     int r = 1;
     byte i = 0;

     while (r > 0 && i < MAXWORDS) {
        r = read_next(words[i]);

        if (words[i].length() > 0)
            i++;
     }

     // if words still left in message read them but don't save.
     if (i == MAXWORDS && r > 0) {
        String waste;
        while (r > 0)
            r = read_next(waste);
     } 
     
     if (r < 0) // bad message
        return 0;

     return i;
}


ISR(TIMER1_OVF_vect)
{
    for (byte i = 0; i < NMOTORS; i++) {
        if (go[i] && count[i]++ >= target[i]) {
            step(i);
            count[i] = 0;
        }
    }
}



void bad_command() {
    Serial.println("bad command");
}

int do_command(byte (*command)(byte, String*), String &motors, String *args)
{
    byte motor = 0;
    byte err = 0;
    for (const char *c = motors.c_str(); *c != 0; c++) {
        motor = *c - 49;
        if (motor < 0 || motor > NMOTORS - 1)
            err = 1;
        else
            err |= command(motor, args);
    }
    return err;
}

byte command_go(byte motor, String args[])
{
    go[motor] = true;
    return 0;
}

byte command_stop(byte motor, String args[])
{

    go[motor] = false;
    return 0;
}

byte command_speed(byte motor, String args[])
{
    target[motor] = args[0].toInt();
    return 0;
}

byte command_speedq(byte motor, String args[])
{
    Serial.println(target[motor]);
    return 0;
}

byte command_dirq(byte motor, String args[])
{
    Serial.println(target[motor]);
    return 0;
}

byte command_dir(byte motor, String args[])
{
    byte dir;
    switch (args[0][0]) {
        case '1':
        case 'i':
        case 'I':
            dir = HIGH;
            break;
        case '0':
        case 'o':
        case 'O':
            dir = LOW;
            break;
        default:
            return 1;
    }
    digitalWrite(DIRS[motor], dir);
    return 0;
}

//struct 


int r = 0;
String words[4];
byte nwords = 0;

void loop() {
    nwords = read_message(words);
    byte err = 0;

    if (nwords < 1)
        return;
    else if (nwords == 2 && (words[0] == "go" || words[0] == "g")) {
        err = do_command(command_go, words[1], 0);
    }
    else if (nwords == 2 && (words[0] == "stop" || words[0] == "s"))
        err = do_command(command_stop, words[1], 0);
    else if (words[0] == "s")
        for (int i = 0; i < NMOTORS; i++)
            command_stop(i, 0);
    else if (nwords == 3 && words[0] == "speed")
        err = do_command(command_speed, words[1], words + 2);
    else if (nwords > 1 && words[0] == "speed?")
        err = do_command(command_speedq, words[1], 0);
    else if (nwords == 3 && words[0] == "dir")
        err = do_command(command_dir, words[1], words + 2);
    else if (nwords > 1 && words[0] == "dir?")
        err = do_command(command_dirq, words[1], words + 2);
    else if (nwords == 2 && words[0] == "echo") {
        if (words[1] == "on" || words[1] == "1")
            echo = true;
        else if (words[1] == "off" || words[1] == "0")
            echo = false;
        else
            err = 1;
    }
    else
        err = 2;
    

    Serial.println(err);
}
