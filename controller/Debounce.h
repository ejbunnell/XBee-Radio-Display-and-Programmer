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