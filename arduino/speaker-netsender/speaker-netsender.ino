#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <ESP_I2S.h>

#define SD_CS_PIN     15 
#define SPI_SCLK_PIN  14 
#define SPI_MISO_PIN  12 
#define SPI_MOSI_PIN  13 

#define I2S_SCK_PIN   33 
#define I2S_DOUT_PIN  23 
#define I2S_WS_PIN    32 

#define I2C_SDA_PIN   16
#define I2C_SCL_PIN   18

#define AMP_ADDRESS   0x2D // 0b0101101

I2SClass i2s;
File audioFile;

void setup() {
  Serial.begin(115200);
  while(!Serial && (millis() < 5000));

  SPI.begin(SPI_SCLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Initialisation failed!");
    while(true);
  }

  if(!Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {
    Serial.println("I2C initialisation failed!");
    while(true);
  }

  // Set pins before begin() for ESP32 v3.x
  i2s.setPins(I2S_SCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);

  if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S Initialisation failed!");
    while(true);
  }

  // This must be done after a stable I2S clock is running.
  init_amp();

  
  Serial.println("Ready. Opening audio.wav...");
}

int init_amp() {
  // Select Page 0.
  Wire.beginTransmission(AMP_ADDRESS);
  Wire.write(0x00); 
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return 1;

  // Select Book 0.
  Wire.beginTransmission(AMP_ADDRESS);
  Wire.write(0x7F);
  Wire.write(0x00);
  Wire.endTransmission();

  // Set device to Hi-Z state before configuration.
  Wire.beginTransmission(AMP_ADDRESS);
  Wire.write(0x03);
  Wire.write(0x02);
  Wire.endTransmission();

  /* Set device settings (1) - offset: 02h
      Bits:
        7:    0   - Reserved
        6-4:  000 - 768K (FSW_SEL)
        3:    0   - Reserved
        2:    1   - PBTL Mode (DAMP_PBTL)
        1-0:  00  - BD Modulation (DAMP_MOD)
        = 0b0000 0100 = 0x04
  */
  Wire.beginTransmission(AMP_ADDRESS);
  Wire.write(0x02); // Register
  Wire.write(0x04); // Config
  Wire.endTransmission();

  /* Set device analog gain - offset: 54h
      Bits:
        7-5:  000   - Reserved
        4-0:  00000 - 0dB (Max Vol) (ANA_GAIN)
        = 0b0000 0000 = 0x00
  */
  Wire.beginTransmission(AMP_ADDRESS);
  Wire.write(0x54);
  Wire.write(0x00);
  Wire.endTransmission();

  /* Set device digital volume - offset: 4Ch
      Bits:
        7-0:  00000000   - MAX Digital Volume
        = 0b0010 0000 = 0x00
  */
  Wire.beginTransmission(AMP_ADDRESS);
  Wire.write(0x4C);
  Wire.write(0x00);
  Wire.endTransmission();

  /* Set device settings (2) - offset: 03h
      Bits:
        7-5:  000 - Reserved
        4:    0   - Don't reset DSP (DIS_DSP)
        3:    0   - Normal Volume (MUTE)
        2:    0   - Reserved
        1-0:  11  - Play (CTRL_STATE)
        = 0b0000 0011 = 0x03
  */  
  Wire.beginTransmission(AMP_ADDRESS);
  Wire.write(0x03);
  Wire.write(0x03);
  Wire.endTransmission();

  // Wait for device to settle (Maybe not required)
  delay(10);

  return 0;
}

void loop() {
  audioFile = SD.open("/audio.wav");

  if (!audioFile) {
    Serial.println("Failed to open audio.wav");
    delay(5000);
    return;
  }

  // Skip the WAV header (44 bytes) to get to raw PCM data
  audioFile.seek(44);

  // Buffer for reading data (must be a multiple of 4 bytes for 16-bit stereo)
  const size_t bufferSize = 512;
  uint8_t buffer[bufferSize];

  Serial.println("Playing...");

  while (audioFile.available()) {
    size_t bytesRead = audioFile.read(buffer, bufferSize);
    
    // Write to I2S. This function blocks until there is space in the DMA buffer.
    size_t bytesWritten = i2s.write(buffer, bytesRead);

    if (bytesWritten == 0) {
      Serial.println("I2S write error");
      break;
    }
  }

  Serial.println("Playback finished.");
  audioFile.close();
  
  delay(2000); // Wait 2 seconds before looping the file again
}