// Link to the docs for the XBee: https://docs.digi.com/resources/documentation/digidocs/pdfs/90001539.pdf?_gl=1*p9jffk*_gcl_au*MzE4NDE0NzU3LjE3NTk5NTc3NjQ.*_ga*MzEzODkxMDYwLjE3NTk5NTc3NjQ.*_ga_RZXDK3PM3B*czE3NjAxMjU4ODkkbzYkZzEkdDE3NjAxMjY5MTkkajQ3JGwwJGgxNzQ0NjU2MTM2

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define i2c_Address 0x3c // This specific display uses this address

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1   //   QT-PY / XIAO
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define CHANNEL_IN_PIN 5 // The pin to connect the switch that toggles between channel 'C' and 'F'
#define BANDWIDTH_IN_PIN 6 // The pin to connect the switch that toggles between bandwidth '555' and '3332'
#define ACTION_PIN 7 // The pin to connect the button that both connects to the XBee and programs it with the selected settings

#define DEBOUNCE_DELAY 60 // This is the delay for the debounce for the three inputs. Higher value means more time required to pass before the program will allow the button to be pressed again. It's in ms

#define CHANNEL_AT_CMD "CH" // The two letter AT (attention) identifier for the channel command
#define BANDWIDTH_AT_CMD "ID" // The two letter AT (attention) identifier for the bandwidth (or pan ID) command
#define FIRMWARE_VERSION_AT_CMD "VR" // The two letter AT (attention) identifier for the firmware version command

#define NO_PARAMETERS "____NO_PARAMETERS____" // A constant char array that allows the sendATCommand function to have a default value for parameters

// Sets the color of the text to white and the background to black
void normalColor()
{
  display.setTextColor(SH110X_WHITE);
}
// Sets the color of the text to black and the background white
void invertedColor()
{
  display.setTextColor(SH110X_BLACK, SH110X_WHITE);
}
// For selections, the color of the text is inverted
// Var will be the option and selected will be the currently selected option
// For example, if var is 0 and selected is 0, the text will be inverted. Otherwise, the text will be normal
void setSelectedColor(int var, int selected)
{
  if (var == selected) invertedColor();
  else normalColor();
}

// Attempt to connect to the XBee by sending the Command Character (CC), '+' three times with one second (GT) of no communication before and after
// The XBee will then respond with "OK\r", which tells us that we are connected. If it does not respond, the screen goes to a "No XBee" screen and waits for the user to press the action button to try again
void connectToXBee();

// Takes the currently selected options for both channel and bandwidth and writes (WR) them to the XBee
void programXBee();
// Pings the XBee to both get the current values of the channel and bandwidth and the make sure that the XBee stays in Command Mode by pinging it within the Command Mode Timeout (CT)
void pingXBee();
// Both updates the current channel and bandwidth of the XBee and the user-selected channel and bandwidth
void updateDisplay();

// Sends an AT command to the XBee. Command is the two character identifier that the XBee uses to determine what to reply with or what to program
// When reading, there are no parameters so you can leave that blank
// When programming the XBee, the parameters are whatever values you are trying to set ('C', 'F', '555', '3332', etc)
void sendATCommand(const char *command, const char *parameters = NO_PARAMETERS);

// Uses sendATCommand with no parameters, but waits for all the returning Serial data and puts it into the char buffer
// The delayMs determines how long the program waits for the Serial signal to come back
// If there is no Serial data to be read, the program puts a -1 in index zero of the buffer
void readATCommand(char *buf, const char *command, int delayMs);

// A quick class to add a debounce to all digital inputs
class Debounce {
  public:
    Debounce(int pin, unsigned int debounceDelay);
    int GetState();
  private:
    int pin;
    int state = HIGH;
    unsigned long debounceTime = 0;
    unsigned int debounceDelay;
    bool canReadLow = true;
};

// The constructor sets the pinMode of the pin to INPUT_PULLUP
Debounce::Debounce(int pin, unsigned int debounceDelay) {
  pinMode(pin, INPUT_PULLUP);
  this->pin = pin;
  this->debounceDelay = debounceDelay;
}

// Gets the current state of the input after applying a debounce
// If digitalRead returns HIGH, it will return HIGH
// If digitalRead returns LOW, it will determine if it has returned LOW within the debounceDelay. If it has, then it will return LOW
int Debounce::GetState() {
  int reading = digitalRead(pin);

  if (canReadLow) state = reading;
  else state = HIGH;

  if (reading == LOW) {
    debounceTime = millis();
    canReadLow = false;
  }

  if ((millis() - debounceTime) > debounceDelay) {
    canReadLow = true;
  }

  return state;
}

Debounce channelButton{CHANNEL_IN_PIN, DEBOUNCE_DELAY};
Debounce bandwidthButton{BANDWIDTH_IN_PIN, DEBOUNCE_DELAY};
Debounce actionButton{ACTION_PIN, DEBOUNCE_DELAY};

void setup()   {
  Serial.begin(9600);

  display.begin(i2c_Address, true);
  display.clearDisplay();
  display.display(); // need this for the display to actually change
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Attempts to connect to the XBee as the program boots up
  connectToXBee();
}

char currentChannel = 'C';
char currentBandwidth[20] = "555";
char firmwareVersion[4] = "";

enum ChannelSelections {C, F};
enum BandwidthSelections {B555, B3332};

ChannelSelections selectedChannel = ChannelSelections::C;
BandwidthSelections selectedBandwidth = BandwidthSelections::B3332;

bool xbeeFound = false;

void loop() {
  // There are two states the program can be in -- with an XBee, or without
  if (xbeeFound) {
    programXBee();
    pingXBee();
    updateDisplay();
  }
  else {
    // Displays the "No XBee" screen
    display.clearDisplay();
    display.setCursor(0, 0);
    normalColor();
    display.print("       ");
    invertedColor();
    display.println("No XBee");
    display.display();

    // Will attempt to connect to the XBee when one isn't connected if the user presses the action button
    if (actionButton.GetState() == LOW) connectToXBee();
  }
}

unsigned long time;

const int okAscii[3] = {'O', 'K', '\r'};

void connectToXBee() {
  // Clears the buffer - probably not needed, but ensures nothing in the way of the "OK" signal
  while (Serial.available()) Serial.read();
  display.setCursor(0, 0);
  display.clearDisplay();
  normalColor();
  display.print("Initializing XBee");
  display.display();
  // Waits the one second before (plus a little extra) and displays dots to show something is happening
  time = millis();
  while ((millis() - time) < 1100) {
    if (millis() % 200 == 0) {
      display.print(".");
      display.display();
    }
  }
  // Sends the command character three times to the XBee
  Serial.write("+++", 3);
  // Waits the one second after (plus a little extra) and displays dots to show something is happening
  time = millis();
  while ((millis() - time ) < 1100) {
    if (millis() % 200 == 0) {
      display.print(".");
      display.display();
    }
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  if (Serial.available()) {
    // If connected becomes false, one of the three characters is not "Ok\r"
    bool connected = true;
    for (int i = 0; i < 3; i++) {
      int read = Serial.read();
      if (read != okAscii[i]) connected = false;
    }
    if (connected) {
      xbeeFound = true;
      display.println("Xbee has successfully entered Command Mode");
    }
    else {
      display.println("Xbee did not confirm entering Command Mode");
    }
  }
  else {
    display.println("Xbee was not found");
  }
  display.display();
  delay(1000);
  
}

void programXBee() {
  // If the channel button is pressed, switch the selected channel
  if (channelButton.GetState() == LOW) {
    if (selectedChannel == ChannelSelections::C) selectedChannel = ChannelSelections::F;
    else selectedChannel = ChannelSelections::C;
  }
  // If the bandwidth button is pressed, switch the selected bandwidth
  if (bandwidthButton.GetState() == LOW) {
    if (selectedBandwidth == BandwidthSelections::B555) selectedBandwidth = BandwidthSelections::B3332;
    else selectedBandwidth = BandwidthSelections::B555;
  }
  // If the action button is NOT pressed, do not program the XBee
  if (actionButton.GetState() == HIGH) return;

  if (selectedChannel == ChannelSelections::C) sendATCommand(CHANNEL_AT_CMD, "C");
  else sendATCommand(CHANNEL_AT_CMD, "F");
  sendATCommand("WR"); // writes (WR) the data to the XBee's flash (can only do 10,000 times per XBee but I don't think that would ever happen -- just something to note)
  // Need a delay to wait for the Serial data to come back from the XBee. Not sure how long exactly it will take, but the whole program isn't intensive so better to wait longer than not
  delay(50);
  // Flushes the Serial buffer from the "OK" signal the write sends back
  while (Serial.available()) { Serial.read(); }

  if (selectedBandwidth == BandwidthSelections::B555) sendATCommand(BANDWIDTH_AT_CMD, "555");
  else sendATCommand(BANDWIDTH_AT_CMD, "3332");
  sendATCommand("WR");
  delay(50);
  while (Serial.available()) { Serial.read(); }
}

void pingXBee() {
  // Flushes the Serial buffer just incase -- probably really don't need this but it is a safety net
  while (Serial.available()) Serial.read();
  // Creates a temporary buffer to hold the character that the XBee returns
  char buf[2];
  readATCommand(buf, CHANNEL_AT_CMD, 20);
  currentChannel = buf[0];
  // readATCommand puts -1 in index zero of the buffer if there was no Serial buffer to read, which means the program could no longer communicate with the XBee
  if (currentChannel == -1) xbeeFound = false;

  readATCommand(currentBandwidth, BANDWIDTH_AT_CMD, 50);
  if (currentBandwidth[0] == -1) xbeeFound = false;

  readATCommand(firmwareVersion, FIRMWARE_VERSION_AT_CMD, 50);
  if (firmwareVersion[0] == -1) xbeeFound = false;
}

void updateDisplay() {
  // Clears all previous text
  display.clearDisplay();

  normalColor();
  display.setCursor(0, 0);
  display.print("Channel: ");
  display.println(currentChannel);

  display.print("Bandwidth: ");
  display.println(currentBandwidth);

  display.print("Firmware: ");
  display.println(firmwareVersion);

  display.println();

  display.println("Desired Channel: "); // C or F
  display.print("     ");
  // Sets either normal or inverted depending on which is selected
  setSelectedColor(ChannelSelections::C, selectedChannel);
  display.print(" C ");

  normalColor();
  display.print("      ");

  setSelectedColor(ChannelSelections::F, selectedChannel);
  display.println(" F ");

  normalColor();
  display.println("Desired Bandwidth: "); // 555 or 3332
  display.print("    ");
  Serial.println(selectedBandwidth);
  // setSelectedColor(BandwidthSelections::B555, selectedBandwidth);
  if (selectedBandwidth == BandwidthSelections::B555) invertedColor();
  display.print(" 555 ");

  normalColor();
  display.print("   ");

  setSelectedColor(BandwidthSelections::B3332, selectedBandwidth);
  display.println(" 3332 ");

  display.display();
}

void sendATCommand(const char *command, const char *parameters = NO_PARAMETERS) {
  // Need the "AT" before every command to get the attention of the XBee
  Serial.write("AT");
  // Immediately after the "AT" needs to be the command characters
  Serial.write(command);
  // If there are any parameters, write them immediately after the command characters
  if (parameters != NO_PARAMETERS) Serial.write(parameters);
  // Every command ends in the carriage return
  Serial.write('\r');
  Serial.flush();
}

void readATCommand(char *buf, const char *command, int delayMs) {
  // Sends the getter AT command with no parameters
  sendATCommand(command);
  delay(delayMs);
  int count = 0;
  // Adds the collected Serial data to the buffer
  while (Serial.available()) {
    char read = Serial.read();
    buf[count] = read;
    count++;
  }
  // If there is no Serial data, the program is no longer talking to the XBee, which means index zero should be -1
  if (count == 0) buf[0] = -1;
}