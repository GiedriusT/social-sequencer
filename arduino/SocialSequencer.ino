#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

// Tempo of an internal clock
#define INTERNAL_CLOCK_BPM 240L
// Use external MIDI clock signal instead of an internal clock (not yet implemented)
#define USE_EXTERNAL_MIDI_CLOCK false

// If debug mode is on various info is printed to Serial port
#define DEBUG_MODE true

// If sketch is started with CALIBRATION_MODE set to true it will wais for two seconds, pweform calibration once, print matrixNoFieldVoltages array to Serial port and brick
#define CALIBRATION_MODE false
// Duration of calibration procedure
#define CALIBRATION_DURATION 8000

// Number of boards connected to the brain
#define BOARD_COUNT 2
// Number of supported cube types
#define CUBE_TYPE_COUNT 4
// Number of columns in the board
#define COL_COUNT 8
// Number of row in the board
#define ROW_COUNT 5

// Analog output with no applied field, enable CALIBRATION_MODE and paste results here
long matrixNoFieldVoltages[][ROW_COUNT][COL_COUNT] = {
  
  // Board 0
  {
    {48196, 48900, 49300, 48292, 49600, 49000, 48800, 49611},
    {49143, 48299, 49362, 49014, 48298, 48894, 49023, 48600},
    {48811, 48208, 48499, 49473, 49466, 48808, 49426, 49400},
    {49354, 49065, 48599, 49124, 48892, 48869, 47599, 49649},
    {49095, 48903, 49203, 49176, 49097, 49189, 49498, 49125}
  },

  // Board 1
  {
    {49156, 49424, 48097, 49601, 48698, 48534, 48299, 48696},
    {48309, 49193, 48594, 48993, 48600, 48499, 48102, 48799},
    {48399, 48698, 48596, 48903, 48702, 48500, 48829, 48699},
    {49298, 48793, 49298, 48799, 48597, 48100, 49076, 49568},
    {48802, 48597, 49159, 49299, 48744, 48700, 49301, 48532}
  }

};

// MIDI note mappings for different cube types
const byte midiNotes[][CUBE_TYPE_COUNT][ROW_COUNT] = {

  // Board 0
  {
    {0,  1,  2,  3,  4},
    {5,  6,  7,  8,  9},
    {10, 11, 12, 13, 14},
    {15, 16, 17, 18, 19}
  },

  // Board 1
  {
    {20, 21, 22, 23, 24},
    {25, 26, 27, 28, 29},
    {30, 31, 32, 33, 34},
    {35, 36, 37, 38, 39}
  }

};

// Pins to which LED strip data channels of the boards are connected
const int LED_STRIP_CONTROL_PINS[] = {22, 24};

// Pins to which main multiplexer outputs of each board are connected
const int BOARD_INPUT_PINS[] = {A0, A1, A2};

// First dimention is board index, second is control line (0 = A, 1 = B, 2 = C)
const int MAIN_MUX_CONTROL_PINS[][3] = {
  {37, 39, 41}, // Board 0
  {36, 38, 40} // Board 1
};

// First dimention is board index, second is control line (0 = A, 1 = B, 2 = C)
const int SUB_MUX_CONTROL_PINS[][3] = {
  {31, 33, 35}, // Board 0
  {30, 32, 34} // Board 1
};

// Defines for multiplexer line names just for confort
#define LINE_A 0
#define LINE_B 1
#define LINE_C 2

// If Hall sensor voltage jumps by more than VOLTAGE_SPIKE_TRESHOLD and stays above for MAX_VOLTAGE_SPIKE_DURATION measuring cycles we no longer count it as a random spike and consider that this is a legit change in magnetic field
#define MAX_VOLTAGE_SPIKE_DURATION 3
// Amount (in voltage * 100L) the voltage has to jump for it to be considered a spike
#define VOLTAGE_SPIKE_TRESHOLD 2000L
// Maximum number of samples from which the average voltage is calculated
#define VOLTAGE_AVERAGING_SAMPLE_LIMIT 200


#define VOLTAGE_MEASURING_TIMEOUT_AFTER_TICK 20 // In milliseconds
#define VOLTAGE_MEASURING_AFTER_MAIN_MUX_SWITCH_TIMEOUT 2 // In milliseconds


// Number of LEDs in LED strip of each board
const int LED_STRIP_PIXEL_COUNT = 48;

// LED strip objects of each board
Adafruit_NeoPixel ledStrips[] = {

  Adafruit_NeoPixel(LED_STRIP_PIXEL_COUNT, LED_STRIP_CONTROL_PINS[0], NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(LED_STRIP_PIXEL_COUNT, LED_STRIP_CONTROL_PINS[1], NEO_GRB + NEO_KHZ800)

};

// How often to update LED strip colors (in milliseconds)
#define LED_STRIP_UPDATE_FREQUENCY 20
// Last LED strip color update time
unsigned long lastLEDStripUpdateTime;
// LED strip iterator used for achieving rainbow cycle effect
int ledStripIterator = 0;

// FX strip is scraped for now, will need to sort this mess out later
#define FX_STRIP_LED_FADE_SPEED 20
#define FX_STRIP_SEGMENT_LED_WHEEL_DIFFERENCE 20

#define FX_STRIP_SEGMENT_COUNT 4
#define FX_STRIP_LEDS_PER_SEGMENT 3
const int FX_STRIP_LED_COUNT = FX_STRIP_SEGMENT_COUNT * FX_STRIP_LEDS_PER_SEGMENT;

int fxStripLEDBrightness[FX_STRIP_LED_COUNT];
uint8_t fxStripLEDColors[FX_STRIP_LED_COUNT][3];
long fxStripLEDLastChangeTime[FX_STRIP_LED_COUNT];
int fxStripLastSegment = 0;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(FX_STRIP_LED_COUNT, 26, NEO_GRB + NEO_KHZ800);

// Signal definitions for multiplexer control lines, first dimention is channel (0-7), second is a control line (0 = A, 1 = B, 2 = C)
const bool MUX_CONTROL_SIGNALS[][3] = {
  {false, false, false}, // Channel 0
  {true,  false, false}, // Channel 1
  {false, true,  false}, // Channel 2
  {true,  true,  false}, // Channel 3
  {false, false, true},  // Channel 4
  {true,  false, true},  // Channel 5
  {false, true,  true},  // Channel 6
  {true,  true,  true}   // Channel 7
};

// Array for storing number of samples no field voltage of each Hall sensor is calculated from
long matrixNoFieldVoltageSampleCount[BOARD_COUNT][ROW_COUNT][COL_COUNT];

// Array for storing how many times the voltage spike was encoutered in a row
int matrixVoltageSpikeCount[BOARD_COUNT][ROW_COUNT][COL_COUNT];
// Array for storing number of samples averaged voltage of each Hall sensor is calculated from
long matrixAverageVoltageSampleCount[BOARD_COUNT][ROW_COUNT][COL_COUNT];
// Array that holds the averaged current voltages across the boards
long matrixAverageVoltages[BOARD_COUNT][ROW_COUNT][COL_COUNT];
// Array that is holding current state of the matrix (what cube types are placed where)
int matrixStates[BOARD_COUNT][ROW_COUNT][COL_COUNT];

// This is used to convert the analog voltage reading to milliGauss
#define TOMILLIGAUSS 3756L  // For A1302: 1.3mV = 1Gauss, and 1024 analog steps = 5V, so 1 step = 3756mG

// Index of currently active column
int activeCol = COL_COUNT - 1;
// Index of column that was active before current one
int prevCol = COL_COUNT - 2;
// Index of column that will be active next
int nextCol = 0;

int activeRow = 0;

// Time last tick was invoked
unsigned long lastTickTime;
// Duration of one tick
unsigned long tickDuration = 60L * 1000L / INTERNAL_CLOCK_BPM;

unsigned long lastMainMuxRowSwitchTime;
unsigned long mainMuxSwitchDuration;
bool isFirstIterationAfterVoltageMeasuringTimeout = true;

uint32_t activeColor;
uint32_t colorWheel[256];
int colorWheelConstant = 256 / LED_STRIP_PIXEL_COUNT;

#define CALIBRATION_BUTTON_PIN 53
#define CALIBRATION_DURATION_IN_CYCLES 2

bool doStartCalibrationModeInNextCycle = false;
bool inCalibrationMode = false;
int currentCalibrationModeCycle;

#define INDOCATOR_BLINK_DURATION_DURING_CALIBRATION 20
unsigned long indicatorLEDLastChngeTime;
bool indicatorLEDState = false;

unsigned long lastFrameTime;

void initColorConstants() {

  for (int i=0; i<256; i++) {

    colorWheel[i] = Wheel(i);
    
  }

  activeColor = ledStrips[0].Color(255, 255, 255);
  
}

void initIndicatorLED() {

  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);
  
}

void initCalibrationButton() {

  pinMode(CALIBRATION_BUTTON_PIN, INPUT_PULLUP);
  
}

void updateIndicatorLED() {

  if (inCalibrationMode || doStartCalibrationModeInNextCycle) {

    if (millis() - indicatorLEDLastChngeTime > INDOCATOR_BLINK_DURATION_DURING_CALIBRATION) {

      indicatorLEDState = !indicatorLEDState;

      digitalWrite(13, indicatorLEDState ? HIGH : LOW);

      indicatorLEDLastChngeTime = millis();
      
    }
    
  }
  
}

void setup() {

  Serial.begin(9600);

  Serial1.begin(31250);

  lastTickTime = millis();
  lastLEDStripUpdateTime = millis();

  initBoards();
  initFXStrip();
  initColorConstants();
  initIndicatorLED();
  initCalibrationButton();

  initMatrixVoltageArray();

  if (CALIBRATION_MODE) {
    
    delay(2000);
    performCalibration();
    printNoFieldVoltageArray();
    while (1);
    
  }

  lastFrameTime = millis();

}

void loop() {

  // Serial.println(millis() - lastFrameTime);

  lastFrameTime = millis();

  updateMatrixVoltageArray();
  processLEDStrips();

  if (USE_EXTERNAL_MIDI_CLOCK)
    processExternalMIDIClock();
  else
    processInternalClock();

  // processFXStrip();
  // strip.show();

  updateIndicatorLED();

}

/******************************************************************************

CORE FUNCTIONS

******************************************************************************/

int getCubeTypeByRelativeVoltage(long voltage) {

  if (voltage > 2000)
    return 3;
  else if (voltage < -2000)
    return 2;

  return 0;

}

int _t_measureCount = 0;

void onTick() {

//  Serial.print(activeRow);
//  Serial.print(" - ");
//  Serial.println(_t_measureCount);

  incrementColIndexes();
  activeRow = 0;

  bool noteBroadcasted = false;

  int currentBoardNr;
  int currentRow;
  int cubeType;

  for (currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

    setActiveSubMuxCol(currentBoardNr, nextCol);
    setActiveMainMuxRow(currentBoardNr, activeRow);

    silencePreviousColNotes(currentBoardNr);

    for (currentRow = 0; currentRow < ROW_COUNT; currentRow++) {

      cubeType = getCubeTypeByRelativeVoltage(matrixAverageVoltages[currentBoardNr][currentRow][activeCol] - matrixNoFieldVoltages[currentBoardNr][currentRow][activeCol]);

      // _debugPrintCRV(activeCol, currentRow, matrixAverageVoltageSampleCount[currentBoardNr][currentRow][activeCol]);

      matrixAverageVoltageSampleCount[currentBoardNr][currentRow][activeCol] = 0;

      if (inCalibrationMode) {

        matrixNoFieldVoltageSampleCount[currentBoardNr][currentRow][activeCol] = 0;
        
      }

      if (!inCalibrationMode && !doStartCalibrationModeInNextCycle) {

        if (cubeType != 0 && matrixStates[currentBoardNr][currentRow][activeCol] == 0) {
  
          onNoteOn(currentBoardNr, currentRow, activeCol, cubeType);
  
        }

        if (DEBUG_MODE) {
  
          _debugPrintCRV(activeCol, currentRow, (matrixAverageVoltages[currentBoardNr][currentRow][activeCol] - matrixNoFieldVoltages[currentBoardNr][currentRow][activeCol]) / 100);
  //        _debugPrintCRV(activeCol, currentRow, gaussValue);
  
        }

      } else {

        if (DEBUG_MODE)
          Serial.println("Calibrating...");
        
      }

    }

    if (DEBUG_MODE)
      Serial.println();

  }

  updateLEDStrips();

  processCalibrationButton();

  isFirstIterationAfterVoltageMeasuringTimeout = true;

}



void updateMatrixVoltageArray() {

  if (millis() - lastTickTime > VOLTAGE_MEASURING_TIMEOUT_AFTER_TICK) {

    if (isFirstIterationAfterVoltageMeasuringTimeout) {

      mainMuxSwitchDuration = (tickDuration - (millis() - lastTickTime)) / ROW_COUNT;

//      Serial.println(mainMuxSwitchDuration);

      isFirstIterationAfterVoltageMeasuringTimeout = false;

      lastMainMuxRowSwitchTime = millis();

      _t_measureCount = 0;
      
    }

    bool doSwitchRow = ((millis() - lastMainMuxRowSwitchTime) >= mainMuxSwitchDuration) && activeRow != (ROW_COUNT - 1);

    int nextRow = (activeRow + 1) % ROW_COUNT;

    if (millis() - lastMainMuxRowSwitchTime > VOLTAGE_MEASURING_AFTER_MAIN_MUX_SWITCH_TIMEOUT || activeRow == 0) {

      _t_measureCount++;
    
      for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {
    
        long tempVoltage = analogRead(BOARD_INPUT_PINS[currentBoardNr]) * 100L;

        if (!inCalibrationMode) {
          
          if (matrixAverageVoltageSampleCount[currentBoardNr][activeRow][nextCol] < VOLTAGE_AVERAGING_SAMPLE_LIMIT)
            matrixAverageVoltageSampleCount[currentBoardNr][activeRow][nextCol]++;
      
          if (matrixAverageVoltageSampleCount[currentBoardNr][activeRow][nextCol] == 1)
            matrixAverageVoltages[currentBoardNr][activeRow][nextCol] = tempVoltage;
          
          matrixAverageVoltages[currentBoardNr][activeRow][nextCol] = (tempVoltage + matrixAverageVoltages[currentBoardNr][activeRow][nextCol] * matrixAverageVoltageSampleCount[currentBoardNr][activeRow][nextCol]) / (matrixAverageVoltageSampleCount[currentBoardNr][activeRow][nextCol] + 1);
          
        } else {

          if (matrixNoFieldVoltageSampleCount[currentBoardNr][activeRow][nextCol] < VOLTAGE_AVERAGING_SAMPLE_LIMIT)
            matrixNoFieldVoltageSampleCount[currentBoardNr][activeRow][nextCol]++;

          if (matrixNoFieldVoltageSampleCount[currentBoardNr][activeRow][nextCol] == 1)
            matrixNoFieldVoltages[currentBoardNr][activeRow][nextCol] = tempVoltage;
            
          matrixNoFieldVoltages[currentBoardNr][activeRow][nextCol] = (tempVoltage + matrixNoFieldVoltages[currentBoardNr][activeRow][nextCol] * matrixNoFieldVoltageSampleCount[currentBoardNr][activeRow][nextCol]) / (matrixNoFieldVoltageSampleCount[currentBoardNr][activeRow][nextCol] + 1L);
          
        }
  
        if (doSwitchRow)
          setActiveMainMuxRow(currentBoardNr, nextRow);
      }

    }

    if (doSwitchRow) {

//      Serial.print(activeRow);
//      Serial.print(" - ");
//      Serial.println(_t_measureCount);
      
      activeRow = nextRow;
      lastMainMuxRowSwitchTime = millis();    

      _t_measureCount = 0;
      
    }

  } else {

    // Serial.println(millis() - lastTickTime);
    
  }
  
}

void onNoteOn(int boardIndex, int row, int col, int cubeType) {

  noteOn(1, midiNotes[boardIndex][cubeType - 1][row], 0x7F);

  matrixStates[boardIndex][row][col] = cubeType;

}

void silencePreviousColNotes(int boardIndex) {

  for (int row = 0; row < ROW_COUNT; row++) {

    if (matrixStates[boardIndex][row][prevCol] > 0) {

      noteOff(1, midiNotes[boardIndex][matrixStates[boardIndex][row][prevCol] - 1][row]);
      matrixStates[boardIndex][row][prevCol] = 0;

    }

  }

}

void processExternalMIDIClock() {



}

void processInternalClock() {

  if (millis() - lastTickTime > tickDuration) {

//    Serial.print(millis() - (lastTickTime + tickDuration));
//    Serial.print(" - ");

    onTick();

    lastTickTime = lastTickTime + tickDuration;

//    Serial.println(millis() - lastTickTime);

  }

}

/******************************************************************************

INITIALIZATION FUNCTIONS

******************************************************************************/

void initMatrixStates(int boardIndex) {

  for (int currentCol = 0; currentCol < COL_COUNT; currentCol++) {

    for (int currentRow = 0; currentRow < ROW_COUNT; currentRow++) {

      matrixStates[boardIndex][currentRow][currentCol] = 0;

    }

  }

}

void initBoards() {

  for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

    initSubMuxes(currentBoardNr);
    initMainMuxes(currentBoardNr);
    initMatrixStates(currentBoardNr);
    ledStrips[currentBoardNr].begin();

  }

}

void initMatrixVoltageArray() {

  for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

    for (int currentCol = 0; currentCol < COL_COUNT; currentCol++) {

      for (int currentRow = 0; currentRow < ROW_COUNT; currentRow++) {

        matrixAverageVoltageSampleCount[currentBoardNr][currentRow][currentCol] = 0;
        matrixAverageVoltages[currentBoardNr][currentRow][currentCol] = matrixNoFieldVoltages[currentBoardNr][currentRow][currentCol];
        
        matrixVoltageSpikeCount[currentBoardNr][currentRow][currentCol] = 0;

      }

    }

  }
  
}

/******************************************************************************

LED STRIP FUNCTIONS

******************************************************************************/

void setPixelBatchColor(int boardIndex, int batchIndex, uint32_t color) {

  for (int i = batchIndex * 3; i < (batchIndex + 1) * 3; i++) {

    ledStrips[boardIndex].setPixelColor(i, color);

  }

    for (int i = 47 - batchIndex * 3; i < 47 - (batchIndex + 1) * 3; i++) {

    ledStrips[boardIndex].setPixelColor(i, color);

  }

}

void updateLEDStrips() {

  for (int stripIndex = 0; stripIndex < BOARD_COUNT; stripIndex++) {

    for (int i=0; i< LED_STRIP_PIXEL_COUNT; i++) {

      ledStrips[stripIndex].setPixelColor(i, colorWheel[((i * colorWheelConstant) + ledStripIterator) & 255]);
    
    }

    setPixelBatchColor(stripIndex, activeCol, activeColor);
  }

  for (int stripIndex = 0; stripIndex < BOARD_COUNT; stripIndex++) {
    
    ledStrips[stripIndex].show();
  
  }
  
}

void processLEDStrips() {

  if (millis() - lastLEDStripUpdateTime >= LED_STRIP_UPDATE_FREQUENCY) {

    ledStripIterator = ledStripIterator + 1 % (255 * 5);
    updateLEDStrips();

    lastLEDStripUpdateTime = millis();
    
  }
  
}

/******************************************************************************

FX STRIP FUNCTIONS

******************************************************************************/

void initFXStrip() {

  for (int i=0; i<FX_STRIP_LED_COUNT; i++) {

    fxStripLEDBrightness[i] = 0;
    fxStripLEDColors[i][0] = 0;
    fxStripLEDColors[i][1] = 0;
    fxStripLEDColors[i][2] = 0;
    fxStripLEDLastChangeTime[i] = 0;
    
  }

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  
}

uint8_t splitColor ( uint32_t c, char value )
{
  switch ( value ) {
    case 'r': return (uint8_t)(c >> 16);
    case 'g': return (uint8_t)(c >>  8);
    case 'b': return (uint8_t)(c >>  0);
    default:  return 0;
  }
}

void lightUpFXStripSegment(int segmentIndex, byte wheelPosition) {

     for(uint8_t j=FX_STRIP_LEDS_PER_SEGMENT * segmentIndex; j < FX_STRIP_LEDS_PER_SEGMENT * (segmentIndex + 1); j++) {

        uint32_t newColor = Wheel((wheelPosition + (FX_STRIP_SEGMENT_LED_WHEEL_DIFFERENCE * j - FX_STRIP_LEDS_PER_SEGMENT * segmentIndex)) % 255);

        fxStripLEDBrightness[j] = 255;
        fxStripLEDColors[j][0] = splitColor(newColor, 'r');
        fxStripLEDColors[j][1] = splitColor(newColor, 'g');
        fxStripLEDColors[j][2] = splitColor(newColor, 'b');
        fxStripLEDLastChangeTime[j] = millis();

        // strip.setPixelColor(j, newColor);


      
        strip.setPixelColor(j, fxStripLEDColors[j][0], fxStripLEDColors[j][1], fxStripLEDColors[j][2]);
        
     }
  
}

void onTickFXStrip() {

  int randomSegmentIndex = random(0, FX_STRIP_SEGMENT_COUNT);

  if (randomSegmentIndex == fxStripLastSegment) {

    randomSegmentIndex = (randomSegmentIndex + 1) % FX_STRIP_SEGMENT_COUNT;
    
  }
  
  byte randomWheelPosition = random(0, 255);

  lightUpFXStripSegment(randomSegmentIndex, randomWheelPosition);

  fxStripLastSegment = randomSegmentIndex;
  
}

void processFXStrip() {

  for (int i=0; i<FX_STRIP_LED_COUNT; i++) {

    if (fxStripLEDBrightness[i] > 0) {

      if (1) {

        fxStripLEDBrightness[i] -= FX_STRIP_LED_FADE_SPEED;
        if (fxStripLEDBrightness[i] < 0)
          fxStripLEDBrightness[i] = 0;
        fxStripLEDLastChangeTime[i] = millis();

        strip.setPixelColor(i, (int)((float)fxStripLEDColors[i][0] / 255 * fxStripLEDBrightness[i]), (int)((float)fxStripLEDColors[i][1] / 255 * fxStripLEDBrightness[i]), (int)((float)fxStripLEDColors[i][2] / 255 * fxStripLEDBrightness[i]));
        
      }
    
    }
    
  }
  
}

/******************************************************************************

MUX CONTROL FUNCTIONS

******************************************************************************/

void initSubMuxes(int boardIndex) {

  pinMode(SUB_MUX_CONTROL_PINS[boardIndex][LINE_A], OUTPUT);
  pinMode(SUB_MUX_CONTROL_PINS[boardIndex][LINE_B], OUTPUT);
  pinMode(SUB_MUX_CONTROL_PINS[boardIndex][LINE_C], OUTPUT);

  setActiveSubMuxCol(boardIndex, 0);

}

void setSubMuxes(int boardIndex, bool lineA, bool lineB, bool lineC) {

  digitalWrite(SUB_MUX_CONTROL_PINS[boardIndex][LINE_A], lineA ? HIGH : LOW);
  digitalWrite(SUB_MUX_CONTROL_PINS[boardIndex][LINE_B], lineB ? HIGH : LOW);
  digitalWrite(SUB_MUX_CONTROL_PINS[boardIndex][LINE_C], lineC ? HIGH : LOW);

}

void setActiveSubMuxCol(int boardIndex, int colNr) {

  setSubMuxes(boardIndex, MUX_CONTROL_SIGNALS[colNr][LINE_A], MUX_CONTROL_SIGNALS[colNr][LINE_B], MUX_CONTROL_SIGNALS[colNr][LINE_C]);

}

void initMainMuxes(int boardIndex) {

  pinMode(MAIN_MUX_CONTROL_PINS[boardIndex][LINE_A], OUTPUT);
  pinMode(MAIN_MUX_CONTROL_PINS[boardIndex][LINE_B], OUTPUT);
  pinMode(MAIN_MUX_CONTROL_PINS[boardIndex][LINE_C], OUTPUT);

  setActiveMainMuxRow(boardIndex, 0);

}

void setMainMuxes(int boardIndex, bool lineA, bool lineB, bool lineC) {

  digitalWrite(MAIN_MUX_CONTROL_PINS[boardIndex][LINE_A], lineA ? HIGH : LOW);
  digitalWrite(MAIN_MUX_CONTROL_PINS[boardIndex][LINE_B], lineB ? HIGH : LOW);
  digitalWrite(MAIN_MUX_CONTROL_PINS[boardIndex][LINE_C], lineC ? HIGH : LOW);

}

void setActiveMainMuxRow(int boardIndex, int rowNr) {

  setMainMuxes(boardIndex, MUX_CONTROL_SIGNALS[rowNr][LINE_A], MUX_CONTROL_SIGNALS[rowNr][LINE_B], MUX_CONTROL_SIGNALS[rowNr][LINE_C]);

}

/******************************************************************************

HELPER FUNCTIONS

******************************************************************************/

void onCycleEnded() {

  if (doStartCalibrationModeInNextCycle) {

    doStartCalibrationModeInNextCycle = false;

    inCalibrationMode = true;
    currentCalibrationModeCycle = 0;
    
  }

  if (inCalibrationMode) {

    if (currentCalibrationModeCycle < CALIBRATION_DURATION_IN_CYCLES) {

      currentCalibrationModeCycle++;
      
    } else {

      inCalibrationMode = false;
      digitalWrite(13, LOW);
      
    }
    
  }
  
}

void incrementColIndexes() {

  prevCol = activeCol;
  activeCol = (activeCol + 1) % COL_COUNT;
  nextCol = (activeCol + 1) % COL_COUNT;

  if (activeCol == 0)
    onCycleEnded();
  
}

int getPreviousCol() {

  int previousCol = activeCol - 1;

  if (previousCol < 0)
    previousCol = COL_COUNT - 1;

  return previousCol;

}

long convertToGauss (int voltageOffset) {

  return voltageOffset * TOMILLIGAUSS / 1000 / 100;

}

uint32_t Wheel(byte WheelPos) {

  WheelPos = 255 - WheelPos;

  if (WheelPos < 85) {
    return ledStrips[0].Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }

  if (WheelPos < 170) {
    WheelPos -= 85;
    return ledStrips[0].Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }

  WheelPos -= 170;

  return ledStrips[0].Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

/******************************************************************************

MIDI FUNCTIONS

******************************************************************************/

// Send a MIDI note on message
void noteOn(byte channel, byte pitch, byte velocity) {
  
  // 0x90 is the first of 16 note on channels. Subtract one to go from MIDI's 1-16 channels to 0-15
  channel += 0x90 - 1;

  // Ensure we're between channels 1 and 16 for a note on message
  if (channel >= 0x90 && channel <= 0x9F) {

    Serial1.write(channel);
    Serial1.write(pitch);
    Serial1.write(velocity);
    
  }
  
}

// Send a MIDI note off message
void noteOff(byte channel, byte pitch) {
  
  // 0x80 is the first of 16 note off channels. Subtract one to go from MIDI's 1-16 channels to 0-15
  channel += 0x80 - 1;

  // Ensure we're between channels 1 and 16 for a note off message
  if (channel >= 0x80 && channel <= 0x8F) {
    
    Serial1.write(channel);
    Serial1.write(pitch);
    Serial1.write((byte)0x00);
    
  }
  
}

// Send a MIDI control change message
void controlChange(byte channel, byte control, byte value) {
  
  // 0xB0 is the first of 16 control change channels. Subtract one to go from MIDI's 1-16 channels to 0-15
  channel += 0xB0 - 1;

  // Ensure we're between channels 1 and 16 for a CC message
  if (channel >= 0xB0 && channel <= 0xBF) {
    
    Serial1.write(channel);
    Serial1.write(control);
    Serial1.write(value);
    
  }
  
}

/******************************************************************************

SERVICE FUNCTIONS

******************************************************************************/

void processCalibrationButton() {

  if (digitalRead(CALIBRATION_BUTTON_PIN) == LOW && !inCalibrationMode) {

    doStartCalibrationModeInNextCycle = true;
    
  }

}

void performCalibration() {

  if (DEBUG_MODE)
    Serial.println("Performing calibration. Make sure no cubes are placed on any of the boards.");

  long startTime = millis();

  long sampleCounts[BOARD_COUNT][ROW_COUNT][COL_COUNT];

  long iterationCounter = 1;

  do {

    for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

      for (int currentCol = 0; currentCol < COL_COUNT; currentCol++) {

        setActiveSubMuxCol(currentBoardNr, currentCol);

        for (int currentRow = 0; currentRow < ROW_COUNT; currentRow++) {

          setActiveMainMuxRow(currentBoardNr, currentRow);

          long tempVoltage = (long)analogRead(BOARD_INPUT_PINS[currentBoardNr]) * 100L;

          if (iterationCounter == 1) {

            sampleCounts[currentBoardNr][currentRow][currentCol] = 0;
            matrixNoFieldVoltages[currentBoardNr][currentRow][currentCol] = tempVoltage;
            
          } else {

            if (sampleCounts[currentBoardNr][currentRow][currentCol] < VOLTAGE_AVERAGING_SAMPLE_LIMIT)
              sampleCounts[currentBoardNr][currentRow][currentCol]++;
              
            matrixNoFieldVoltages[currentBoardNr][currentRow][currentCol] = (tempVoltage + matrixNoFieldVoltages[currentBoardNr][currentRow][currentCol] * sampleCounts[currentBoardNr][currentRow][currentCol]) / (sampleCounts[currentBoardNr][currentRow][currentCol] + 1L);
            
          }
        }

      }

    }

    if (DEBUG_MODE) {
      
      Serial.print(".");

      if (iterationCounter % 40 == 0)
        Serial.println();

    }

    iterationCounter++;

  } while (millis() - startTime < CALIBRATION_DURATION);

  if (DEBUG_MODE) {

    Serial.println();
    Serial.println("Calibration complete.");
  
  }
  
}

void printNoFieldVoltageArray() {

  Serial.println();
  Serial.println();

  for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

    Serial.print("\n  // Board ");
    Serial.println(currentBoardNr);
    Serial.println("  {");

    for (int currentRow = 0; currentRow < ROW_COUNT; currentRow++) {
    
      Serial.print("    {");

      for (int currentCol = 0; currentCol < COL_COUNT; currentCol++) {

        Serial.print(matrixNoFieldVoltages[currentBoardNr][currentRow][currentCol]);
        if (currentCol < COL_COUNT - 1)
          Serial.print(", ");

      }

      Serial.print("}");
      Serial.println(currentRow == ROW_COUNT - 1 ? "" : ",");

    }

    Serial.print("  }");
    Serial.println(currentBoardNr == BOARD_COUNT - 1 ? "" : ",");

  }
  
}

/******************************************************************************

DEBUG FUNCTIONS

******************************************************************************/

void _debugPrintCRV(int col, int row, long theValue) {

        Serial.print(col);
        Serial.print(",");
        Serial.print(row);
        Serial.print(": ");
        
//        if (cubeType > 0)
//          Serial.print(cubeType);
//        else
//          Serial.print(" ");

        Serial.print(theValue == 0 ? " " : (theValue > 0 ? "+" : ""));
        Serial.print(theValue);
        Serial.print(abs(theValue) < 10 ? " " : "");
        Serial.print(abs(theValue) < 100 ? " " : "");
        Serial.print(abs(theValue) < 1000 ? " " : "");
        Serial.print(abs(theValue) < 10000 ? " " : "");
        Serial.print(" ");
  
}
