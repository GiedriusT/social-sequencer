#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

#define USE_EXTERNAL_MIDI_CLOCK false
#define INTERNAL_CLOCK_BPM 240L

#define DEBUG_MODE true

#define CALIBRATION_MODE false
#define CALIBRATION_DURATION 8000

#define BOARD_COUNT 2
#define COL_COUNT 8
#define ROW_COUNT 5
#define CUBE_TYPE_COUNT 4

// Analog output with no applied field, enable CALIBRATION_MODE and paste results here
long matrixNoFieldVoltages[][ROW_COUNT][COL_COUNT] = {
  


  // Board 0
  {
    {47466, 48093, 48438, 47587, 48515, 48312, 48032, 48811},
    {48145, 47544, 48528, 48198, 47722, 47968, 48111, 48105},
    {48038, 47577, 47662, 48669, 48462, 47964, 48579, 48712},
    {48504, 48066, 47921, 48231, 48093, 48141, 46662, 48865},
    {48118, 48148, 48384, 48349, 48445, 48204, 48781, 48342}
  },

  // Board 1
  {
    {46269, 46432, 45354, 46577, 45863, 45733, 45479, 45941},
    {45341, 46346, 45714, 46160, 45892, 45726, 45314, 45809},
    {45688, 45794, 45715, 46102, 45787, 45884, 46006, 45897},
    {46319, 45878, 46520, 45918, 45836, 45418, 46207, 46735},
    {45972, 45779, 46166, 46383, 45956, 45925, 46479, 45668}
  }




};

// Pins to which main multiplexer outputs of each board are connected
const int BOARD_INPUT_PINS[] = {A0, A1, A2};

// First dimention is board index, second is control line (0 = A, 1 = B, 2 = C)
const int MAIN_MUX_CONTROL_PINS[][3] = {
  {5, 6, 7}, // Board 0
  {11, 12, 13} // Board 1
};

// First dimention is board index, second is control line (0 = A, 1 = B, 2 = C)
const int SUB_MUX_CONTROL_PINS[][3] = {
  {2, 3, 4}, // Board 0
  {8, 9, 10} // Board 1
};

#define MUX_DATA_READ_TIMEOUT 0 // In milliseconds

#define LINE_A 0
#define LINE_B 1
#define LINE_C 2



#define MAX_VOLTAGE_SPIKE_DURATION 3 // In cycles
#define VOLTAGE_SPIKE_TRESHOLD 2000L
#define NO_FIELD_VOLTAGE_OFFSET_TRESHOLD 2000L
#define VOLTAGE_AVERAGING_SAMPLE_LIMIT 200

const int LED_STRIP_PIXEL_COUNT = 48;
const int LED_STRIP_CONTROL_PINS[] = {22, 24};

Adafruit_NeoPixel ledStrips[] = {

  Adafruit_NeoPixel(LED_STRIP_PIXEL_COUNT, LED_STRIP_CONTROL_PINS[0], NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(LED_STRIP_PIXEL_COUNT, LED_STRIP_CONTROL_PINS[1], NEO_GRB + NEO_KHZ800)

};

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

long matrixNoFieldVoltageSampleCount[BOARD_COUNT][ROW_COUNT][COL_COUNT];

int matrixVoltageSpikeCount[BOARD_COUNT][ROW_COUNT][COL_COUNT];
long matrixAverageVoltageSampleCount[BOARD_COUNT][ROW_COUNT][COL_COUNT];
long matrixAverageVoltages[BOARD_COUNT][ROW_COUNT][COL_COUNT];
int matrixStates[BOARD_COUNT][ROW_COUNT][COL_COUNT];
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

#define LED_STRIP_UPDATE_FREQUENCY 20 // In milliseconds
long lastLEDStripUpdateTime;
int ledStripIterator = 0;

#define NOFIELD 511L    // Analog output with no applied field, calibrate this

// This is used to convert the analog voltage reading to milliGauss
//#define TOMILLIGAUSS 1953L  // For A1301: 2.5mV = 1Gauss, and 1024 analog steps = 5V, so 1 step = 1953mG
#define TOMILLIGAUSS 3756L  // For A1302: 1.3mV = 1Gauss, and 1024 analog steps = 5V, so 1 step = 3756mG

int _boardInputVal;
int activeCol = 0;
int prevCol = 0;

long lastTickTime;
long tickDuration = 60L * 1000L / INTERNAL_CLOCK_BPM;

bool tempIsOn = false;

int pixelBatchSize = 3;

byte colorWheelPosition = 0;

void setup() {

  Serial.begin(9600);

  Serial1.begin(31250);

  lastTickTime = millis();
  lastLEDStripUpdateTime = millis();

  initBoards();
  initFXStrip();

  initMatrixVoltageArray();

  if (CALIBRATION_MODE) {
    
    delay(2000);
    performCalibration(); 
    
  }

}

void loop() {

  updateMatrixVoltageArray();
  processLEDStrips();

  if (USE_EXTERNAL_MIDI_CLOCK)
    processExternalMIDIClock();
  else
    processInternalClock();

  // processFXStrip();
  // strip.show();

}

/******************************************************************************

CORE FUNCTIONS

******************************************************************************/

int getCubeTypeByGaussValue(long gauss) {

  if (gauss > 100)
    return 3;
  else if (gauss < -100)
    return 2;

  return 0;

}

void onTick() {

  bool noteBroadcasted = false;

  for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

    prevCol = getPreviousCol();

    silencePreviousColNotes(currentBoardNr);

    for (int currentRow = 0; currentRow < ROW_COUNT; currentRow++) {

      long gaussValue = convertToGauss(matrixAverageVoltages[currentBoardNr][currentRow][activeCol] - matrixNoFieldVoltages[currentBoardNr][currentRow][activeCol]);
      int cubeType = getCubeTypeByGaussValue(gaussValue);

      if (cubeType != 0 && matrixStates[currentBoardNr][currentRow][activeCol] == 0) {

        onNoteOn(currentBoardNr, currentRow, activeCol, cubeType);

//        if (!noteBroadcasted) {
//
//          onTickFXStrip();
//
//          noteBroadcasted = true;
//        
//        }

      }

      if (DEBUG_MODE) {

//        _debugPrintCRV(activeCol, currentRow, matrixAverageVoltages[currentBoardNr][currentRow][activeCol] - matrixNoFieldVoltages[currentBoardNr][currentRow][activeCol]);
        _debugPrintCRV(activeCol, currentRow, gaussValue);

      }

    }

    if (DEBUG_MODE)
      Serial.println();

  }

  updateLEDStrips();

  activeCol = (activeCol + 1) % COL_COUNT;

}

void updateMatrixVoltageArray() {

  for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

    for (int currentCol = 0; currentCol < COL_COUNT; currentCol++) {

      setActiveSubMuxCol(currentBoardNr, currentCol);

      for (int currentRow = 0; currentRow < ROW_COUNT; currentRow++) {

        bool doIgnoreVoltage = false;

        setActiveMainMuxRow(currentBoardNr, currentRow);

        _boardInputVal = analogRead(BOARD_INPUT_PINS[currentBoardNr]);

        long tempVoltage = _boardInputVal * 100L;

        if (abs(tempVoltage - matrixAverageVoltages[currentBoardNr][currentRow][currentCol]) > VOLTAGE_SPIKE_TRESHOLD)
        {
          matrixVoltageSpikeCount[currentBoardNr][currentRow][currentCol]++;

          if (matrixVoltageSpikeCount[currentBoardNr][currentRow][currentCol] > MAX_VOLTAGE_SPIKE_DURATION) {

            matrixAverageVoltageSampleCount[currentBoardNr][currentRow][currentCol] = 0;
            matrixAverageVoltages[currentBoardNr][currentRow][currentCol] = tempVoltage;
            
            matrixVoltageSpikeCount[currentBoardNr][currentRow][currentCol] = 0;
            
          } else {

            doIgnoreVoltage = true;
            
          }
          
        } else {

          matrixVoltageSpikeCount[currentBoardNr][currentRow][currentCol] = 0;

//          if (abs(tempVoltage - matrixNoFieldVoltages[currentBoardNr][currentRow][currentCol]) < VOLTAGE_SPIKE_TRESHOLD) {
//
//            if (matrixNoFieldVoltageSampleCount[currentBoardNr][currentRow][currentCol] < VOLTAGE_AVERAGING_SAMPLE_LIMIT)
//              matrixNoFieldVoltageSampleCount[currentBoardNr][currentRow][currentCol]++;
//              
//            matrixNoFieldVoltages[currentBoardNr][currentRow][currentCol] = (tempVoltage + matrixNoFieldVoltages[currentBoardNr][currentRow][currentCol] * matrixNoFieldVoltageSampleCount[currentBoardNr][currentRow][currentCol]) / (matrixNoFieldVoltageSampleCount[currentBoardNr][currentRow][currentCol] + 1L);
//            
//          }
          
        }

        if (!doIgnoreVoltage) {

          if (matrixAverageVoltageSampleCount[currentBoardNr][currentRow][currentCol] < VOLTAGE_AVERAGING_SAMPLE_LIMIT)
            matrixAverageVoltageSampleCount[currentBoardNr][currentRow][currentCol]++;
          
          matrixAverageVoltages[currentBoardNr][currentRow][currentCol] = (tempVoltage + matrixAverageVoltages[currentBoardNr][currentRow][currentCol] * matrixAverageVoltageSampleCount[currentBoardNr][currentRow][currentCol]) / (matrixAverageVoltageSampleCount[currentBoardNr][currentRow][currentCol] + 1);
          
        }

      }

    }

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

    onTick();

    lastTickTime = lastTickTime + tickDuration;

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

    for (int i=0; i< ledStrips[stripIndex].numPixels(); i++) {
      
      ledStrips[stripIndex].setPixelColor(i, Wheel(((i * 256 / ledStrips[stripIndex].numPixels()) + ledStripIterator) & 255));
    
    }

    // setPixelBatchColor(stripIndex, prevCol, ledStrips[stripIndex].Color(0, 0, 0));
    setPixelBatchColor(stripIndex, activeCol, ledStrips[stripIndex].Color(255, 255, 255));
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

void performCalibration() {

  Serial.print("Performing calibration");

  long startTime = millis();

  long adjustmentMatrix[BOARD_COUNT][ROW_COUNT][COL_COUNT];

  bool isFirstIteration = true;

  long iterationCounter = 1;

  do {

    for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

      for (int currentCol = 0; currentCol < COL_COUNT; currentCol++) {

        setActiveSubMuxCol(currentBoardNr, currentCol);

        for (int currentRow = 0; currentRow < ROW_COUNT; currentRow++) {

          setActiveMainMuxRow(currentBoardNr, currentRow);

          delay(MUX_DATA_READ_TIMEOUT);

          _boardInputVal = analogRead(BOARD_INPUT_PINS[currentBoardNr]);

          if (isFirstIteration)
            adjustmentMatrix[currentBoardNr][currentRow][currentCol] = _boardInputVal * 100L;
          else
            adjustmentMatrix[currentBoardNr][currentRow][currentCol] = (adjustmentMatrix[currentBoardNr][currentRow][currentCol] + (long)_boardInputVal * 100L) / 2L;
        }

      }

    }

    isFirstIteration = false;

    Serial.print(".");

    if (iterationCounter % 40 == 0)
      Serial.println();

    iterationCounter++;

  } while (millis() - startTime < CALIBRATION_DURATION);

  Serial.println();
  Serial.println();

  for (int currentBoardNr = 0; currentBoardNr < BOARD_COUNT; currentBoardNr++) {

    Serial.print("\n  // Board ");
    Serial.println(currentBoardNr);
    Serial.println("  {");

    for (int currentRow = 0; currentRow < ROW_COUNT; currentRow++) {
    

      Serial.print("    {");

      for (int currentCol = 0; currentCol < COL_COUNT; currentCol++) {

        Serial.print(adjustmentMatrix[currentBoardNr][currentRow][currentCol]);
        if (currentCol < COL_COUNT - 1)
          Serial.print(", ");

      }

      Serial.print("}");
      Serial.println(currentRow == ROW_COUNT - 1 ? "" : ",");

    }

    Serial.print("  }");
    Serial.println(currentBoardNr == BOARD_COUNT - 1 ? "" : ",");

  }

  while (1);

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
