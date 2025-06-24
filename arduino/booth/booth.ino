// Arduino Sketch: Coin Machine Controller with Sequence, Set Credit, GPIO7 Logic & Credit Expiry

// --- Pin Definitions ---
const int coin50Pin = 4;    // Digital pin for 50 cents coin (INPUT_PULLUP)
const int coin1EuroPin = 3;   // Digital pin for 1 Euro coin (INPUT_PULLUP)
const int coin2EuroPin = 2;   // Digital pin for 2 Euro coin (INPUT_PULLUP)
const int maxCreditLedPin = 5;  // Digital pin for Max Credit/Locked indicator (OUTPUT)
const int gpios7TriggerPin = 10; // Digital pin for specific HTTP call trigger (INPUT_PULLUP)

// Directional button pins
const int leftPin = 6;
const int upPin = 7;
const int rightPin = 8;
const int downPin = 9;

// --- Coin Values ---
const float value50Cents = 0.50;
const float value1Euro = 1.00;
const float value2Euro = 2.00;
const float creditDeductionAmount = 4.00; // Amount to subtract on GPIO 7 trigger
const float maxCreditThreshold = 4.00; // 4 Euros, also the sequence target

// --- Global Variables ---
float currentCredit = 0.00;
bool lockedByWindows = false; // True if Windows has sent a LOCK_COUNT command

// --- Debounce Variables ---
unsigned long lastDebounceTimeCoin50 = 0;
unsigned long lastDebounceTimeCoin1Euro = 0;
unsigned long lastDebounceTimeCoin2Euro = 0;
unsigned long lastDebounceTimeGpio7 = 0;
unsigned long lastDebounceTimeLeft = 0;
unsigned long lastDebounceTimeUp = 0;
unsigned long lastDebounceTimeRight = 0;
unsigned long lastDebounceTimeDown = 0;

const unsigned long debounceDelay = 100; // milliseconds

String incomingCommand = ""; // Buffer for incoming serial data

// --- Sequence Variables ---
String konamiSequence = "LRUDLRDULR"; // Default 10-button example sequence
String inputSequenceBuffer = "";
const int maxSequenceBufferLength = 20; // Max length to store recent presses

// --- NEW: Credit Expiry Variables ---
unsigned long lastCreditActivityTime = 0; // Stores millis() when credit was last active/changed (and was < 4€)
const unsigned long creditExpiryInterval = 10 * 60 * 1000UL; // 10 minutes in milliseconds

void setup() {
  Serial.begin(9600); // Initialize serial communication

  // Configure input pins with internal pull-up resistors
  pinMode(coin50Pin, INPUT_PULLUP);
  pinMode(coin1EuroPin, INPUT_PULLUP);
  pinMode(coin2EuroPin, INPUT_PULLUP);
  pinMode(gpios7TriggerPin, INPUT_PULLUP);
  pinMode(leftPin, INPUT_PULLUP);
  pinMode(upPin, INPUT_PULLUP);
  pinMode(rightPin, INPUT_PULLUP);
  pinMode(downPin, INPUT_PULLUP);

  // Configure output pin for max credit/locked indicator
  pinMode(maxCreditLedPin, OUTPUT);
  digitalWrite(maxCreditLedPin, LOW); // Start with indicator off

  Serial.println("Arduino Coin Machine Ready!");
  Serial.print("Initial Konami Sequence: ");
  Serial.println(konamiSequence);

  lastCreditActivityTime = millis(); // Initialize credit activity timer
  sendCreditUpdate(); // Send initial credit
}

void loop() {
  // --- Check Coin Inputs ---
  checkCoinInput(coin50Pin, value50Cents, &lastDebounceTimeCoin50);
  checkCoinInput(coin1EuroPin, value1Euro, &lastDebounceTimeCoin1Euro);
  checkCoinInput(coin2EuroPin, value2Euro, &lastDebounceTimeCoin2Euro);

  // --- Check GPIO 7 Trigger Input (with credit deduction) ---
  checkGpio7Trigger();

  // --- Check Directional Button Inputs ---
  checkDirectionalInput(leftPin, 'L', &lastDebounceTimeLeft);
  checkDirectionalInput(upPin, 'U', &lastDebounceTimeUp);
  checkDirectionalInput(rightPin, 'R', &lastDebounceTimeRight);
  checkDirectionalInput(downPin, 'D', &lastDebounceTimeDown);

  // --- After any directional input, check for sequence match ---
  checkKonamiSequence();

  // --- Check for Credit Expiry ---
  checkCreditExpiry();

  // --- Update Max Credit/Locked Indicator (GPIO 6) ---
  updateMaxCreditIndicator();

  // --- Process Incoming Serial Commands from Windows ---
  readSerialCommands();
}

// Function to handle individual coin inputs
void checkCoinInput(int pin, float value, unsigned long* lastDebounceVar) {
  if (digitalRead(pin) == LOW) {


      if (lockedByWindows) {
        Serial.println("DEBUG: Coin ignored - system locked by Windows.");
        return;
      }
      if (currentCredit >= maxCreditThreshold) {
        Serial.println("DEBUG: Coin ignored - max credit reached.");
        return;
      }

      currentCredit += value;
     // if (currentCredit > maxCreditThreshold) {
     //   currentCredit = maxCreditThreshold;
     // }
      Serial.print("DEBUG: Coin detected: "); Serial.print(value);
      Serial.print(", New Credit: "); Serial.println(currentCredit);
      sendCreditUpdate();
      updateCreditActivityTime(); // Update timer if credit is below max threshold
      delay(300);
    
  } else {
    *lastDebounceVar = millis();
  }
}

// Function to handle GPIO 7 trigger input
void checkGpio7Trigger() {
  if (digitalRead(gpios7TriggerPin) == LOW) {
    if (millis() - lastDebounceTimeGpio7 > debounceDelay) {
      lastDebounceTimeGpio7 = millis();

      if (currentCredit >= creditDeductionAmount) {
        currentCredit -= creditDeductionAmount;
        Serial.print("DEBUG: "); Serial.print(creditDeductionAmount, 2);
        Serial.println(" Euros subtracted by GPIO 7 trigger.");
        sendCreditUpdate();
        updateCreditActivityTime(); // Update timer if credit is below max threshold
        Serial.println("GPIO7_TRIGGERED"); // Always send this for the HTTP call
      } else {
        Serial.print("DEBUG: Not enough credit ("); Serial.print(currentCredit, 2);
        Serial.print("€) to subtract "); Serial.print(creditDeductionAmount, 2);
        Serial.println("€ on GPIO 7 trigger.");
      }
    }
  } else {
    lastDebounceTimeGpio7 = millis();
  }
}

// Function to handle directional button inputs
void checkDirectionalInput(int pin, char buttonChar, unsigned long* lastDebounceVar) {
  if (digitalRead(pin) == LOW) {
    if (millis() - *lastDebounceVar > debounceDelay) {
      *lastDebounceVar = millis();
      addCharToSequenceBuffer(buttonChar);
      Serial.print("DEBUG: Directional button pressed: "); Serial.println(buttonChar);
    }
  } else {
    *lastDebounceVar = millis();
  }
}

// Adds a character to the rolling sequence buffer
void addCharToSequenceBuffer(char c) {
  inputSequenceBuffer += c;
  if (inputSequenceBuffer.length() > maxSequenceBufferLength) {
    inputSequenceBuffer = inputSequenceBuffer.substring(1);
  }
}

// Checks if the input buffer contains the konami sequence at its end
void checkKonamiSequence() {
  if (konamiSequence.length() > 0 && inputSequenceBuffer.endsWith(konamiSequence)) {
    Serial.println("DEBUG: Konami Sequence Matched!");
    currentCredit = maxCreditThreshold; // Set credit to 4 Euros
    sendCreditUpdate();
    // No need to update lastCreditActivityTime here, as credit is now >= maxCreditThreshold
    inputSequenceBuffer = ""; // Clear buffer to prevent immediate re-trigger
  }
}

// NEW: Function to check if credit has expired
void checkCreditExpiry() {
  if (!lockedByWindows && currentCredit < maxCreditThreshold) { // Only expire if not locked and below max threshold
    if (millis() - lastCreditActivityTime >= creditExpiryInterval) {
      Serial.println("DEBUG: Credit expired! Resetting to 0.00.");
      currentCredit = 0.00;
      sendCreditUpdate();
      lastCreditActivityTime = millis(); // Reset activity time after expiry
    }
  }
}

// NEW: Function to update the lastCreditActivityTime if credit is below threshold
void updateCreditActivityTime() {
  if (currentCredit < maxCreditThreshold) {
    lastCreditActivityTime = millis();
  }
}

// Function to update the state of the max credit/locked indicator (GPIO 6)
void updateMaxCreditIndicator() {
  if (lockedByWindows || currentCredit >= maxCreditThreshold) {
    digitalWrite(maxCreditLedPin, HIGH); // Turn on indicator
  } else {
    digitalWrite(maxCreditLedPin, LOW);  // Turn off indicator
  }
}

// Function to send current credit to Windows
void sendCreditUpdate() {
  char buffer[10];
  dtostrf(currentCredit, 4, 2, buffer);
  Serial.print("CREDIT_UPDATE:");
  Serial.println(buffer);
}

// Function to read and process incoming serial commands
void readSerialCommands() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    incomingCommand += inChar;
    if (inChar == '\n') {
      processSerialCommand(incomingCommand);
      incomingCommand = "";
    }
  }
}

// Function to process a single serial command
void processSerialCommand(String command) {
  command.trim();
  Serial.print("DEBUG: Received command from Windows: "); Serial.println(command);

  if (command.startsWith("SET_CREDIT:")) {
    String creditStr = command.substring(command.indexOf(":") + 1);
    float newCredit = creditStr.toFloat();
    if (newCredit >= 0.00) {
      currentCredit = newCredit;
      sendCreditUpdate();
      updateCreditActivityTime(); // Update timer if credit is below max threshold
      Serial.print("ACK: Credit set to: "); Serial.println(currentCredit, 2);
    } else {
      Serial.println("ACK: SET_CREDIT: Invalid credit value (must be >= 0).");
    }
  } else if (command.startsWith("SET_SEQUENCE:")) {
    String newSequence = command.substring(command.indexOf(":") + 1);
    if (newSequence.length() > 0) {
      konamiSequence = newSequence;
      inputSequenceBuffer = "";
      Serial.print("ACK: Sequence Updated to: "); Serial.println(konamiSequence);
    } else {
      Serial.println("ACK: SET_SEQUENCE: Invalid sequence provided (empty).");
    }
  } else if (command == "LOCK_COUNT") {
    lockedByWindows = true;
    Serial.println("ACK: System Locked.");
  } else if (command == "UNLOCK_COUNT") {
    lockedByWindows = false;
    // When unlocked, if credit is below max, restart the expiry timer
    if (currentCredit < maxCreditThreshold) {
      lastCreditActivityTime = millis();
    }
    Serial.println("ACK: System Unlocked.");
  } else if (command == "RESET_CREDIT") {
    currentCredit = 0.00;
    sendCreditUpdate();
    lastCreditActivityTime = millis(); // Reset activity time after reset
    Serial.println("ACK: Credit Reset.");
  } else {
    Serial.println("ACK: UNKNOWN_COMMAND");
  }
}