/*
 * pin 1 - not used          |  Micro SD card     |
 * pin 2 - CS (SS)           |                   /
 * pin 3 - DI (MOSI)         |                  |__
 * pin 4 - VDD (3.3V)        |                    |
 * pin 5 - SCK (SCLK)        | 8 7 6 5 4 3 2 1   /
 * pin 6 - VSS (GND)         | ▄ ▄ ▄ ▄ ▄ ▄ ▄ ▄  /
 * pin 7 - DO (MISO)         | ▀ ▀ █ ▀ █ ▀ ▀ ▀ |
 * pin 8 - not used          |_________________|
 *                             ║ ║ ║ ║ ║ ║ ║ ║
 *                     ╔═══════╝ ║ ║ ║ ║ ║ ║ ╚═════════╗
 *                     ║         ║ ║ ║ ║ ║ ╚══════╗    ║
 *                     ║   ╔═════╝ ║ ║ ║ ╚═════╗  ║    ║
 * Connections for     ║   ║   ╔═══╩═║═║═══╗   ║  ║    ║
 * full-sized          ║   ║   ║   ╔═╝ ║   ║   ║  ║    ║
 * SD card             ║   ║   ║   ║   ║   ║   ║  ║    ║
 * Pin name         |  -  DO  VSS SCK VDD VSS DI CS    -  |
 * SD pin number    |  8   7   6   5   4   3   2   1   9 /
 *                  |                                  █/
 *                  |__▍___▊___█___█___█___█___█___█___/
 *
 * Note:  The SPI pins can be manually configured by using `SPI.begin(sck, miso, mosi, cs).`
 *        Alternatively, you can change the CS pin and use the other default settings by using `SD.begin(cs)`.
 *
 * +--------------+---------+-------+----------+----------+----------+----------+----------+
 * | SPI Pin Name | ESP8266 | ESP32 | ESP32‑S2 | ESP32‑S3 | ESP32‑C3 | ESP32‑C6 | ESP32‑H2 |
 * +==============+=========+=======+==========+==========+==========+==========+==========+
 * | CS (SS)      | GPIO15  | GPIO5 | GPIO34   | GPIO10   | GPIO7    | GPIO18   | GPIO0    |
 * +--------------+---------+-------+----------+----------+----------+----------+----------+
 * | DI (MOSI)    | GPIO13  | GPIO23| GPIO35   | GPIO11   | GPIO6    | GPIO19   | GPIO25   |
 * +--------------+---------+-------+----------+----------+----------+----------+----------+
 * | DO (MISO)    | GPIO12  | GPIO19| GPIO37   | GPIO13   | GPIO5    | GPIO20   | GPIO11   |
 * +--------------+---------+-------+----------+----------+----------+----------+----------+
 * | SCK (SCLK)   | GPIO14  | GPIO18| GPIO36   | GPIO12   | GPIO4    | GPIO21   | GPIO10   |
 * +--------------+---------+-------+----------+----------+----------+----------+----------+
 *
 * For more info see file README.md in this library or on URL:
 * https://github.com/espressif/arduino-esp32/tree/master/libraries/SD
 */

#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "ESP_I2S.h"
#include "wav_header.h"
#include "AacEncoder.h"

//Uncomment and set up if you want to use custom pins for the SPI communication
//#define REASSIGN_PINS
int sck = 9;
int miso = 7;
int mosi = 8;
int cs = 10;

const uint8_t I2S_SCK = 41;
const uint8_t I2S_WS = 42;
const uint8_t I2S_DIN = 2;
I2SClass i2s;

const int WAVE_HEADER_SIZE = PCM_WAV_HEADER_SIZE;

void btnCallBack();

// #define AUDIO_BLOCK_SIZE 40000

typedef struct {
  uint8_t *data;
  size_t len;
} audio_block_t;

QueueHandle_t audioQueue;

#define RING_BUFFER_SIZE 51200
uint8_t ringBuffer[RING_BUFFER_SIZE];
uint8_t *ringPointer;

void dataCallback(uint8_t *aac_data, size_t len);

AacEncoder encoder;

void setup() {

  // Create variables to store the audio data

  Serial.begin(115200);

  Serial.println("LoopTaskStackSize=" + String(getArduinoLoopTaskStackSize()));

  Serial.println("Initializing I2S bus...");

  encoder.init(16000, 1, 24000, AacEncoder::ADTS);

  // Set up the pins used for audio input
  i2s.setPins(I2S_SCK, I2S_WS, -1, I2S_DIN);

  // Initialize the I2S bus in standard mode
  if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("Failed to initialize I2S bus!");
    return;
  }

  Serial.println("I2S bus initialized.");

#ifdef REASSIGN_PINS
  SPI.begin(sck, miso, mosi, cs);
  if (!SD.begin(cs)) {
#else
  if (!SD.begin()) {
#endif
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);

  Serial.println("SD card initialized.");

  pinMode(14, INPUT_PULLUP);
  pinMode(40, OUTPUT);
  digitalWrite(40, HIGH);
  audioQueue = xQueueCreate(64, sizeof(audio_block_t));
  attachInterrupt(14, btnCallBack, FALLING);
  ringPointer = ringBuffer;
}

int ledState = LOW;
bool isRecording, isRecording_last;
volatile bool sdTaskRunning;
uint64_t lastTime, nowTime;
size_t wav_size;
File file;
volatile bool recordToggleRequest = false;
volatile int cnt = 0;
int maxSerialNumber = 0;

int getName(fs::FS &fs, const char *dirname, uint8_t levels) {
  static int maxSerialNumber = 0;

  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return -1;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return -1;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {

    } else {
      // Serial.print("  FILE: ");
      // Serial.print(file.name());
      // Serial.print("  SIZE: ");
      // Serial.println(file.size());
      String name = String(file.name());
      if (name.startsWith("record")) {
        int serialNumber = name.substring(6, name.length()).toInt();
        if (serialNumber > maxSerialNumber)
          maxSerialNumber = serialNumber;
      }
    }
    file = root.openNextFile();
  }
  return maxSerialNumber + 1;
}

void createWAV(int serialNumber) {
  String fileName = "/record" + String(serialNumber) + ".aac";
  Serial.println("Creating new record " + fileName);
  file = SD.open(fileName.c_str(), FILE_WRITE);
}

uint8_t *RecordMicroseconds(int rec_microseconds, size_t *out_size) {
  uint32_t sample_rate = i2s.rxSampleRate();
  uint16_t sample_width = (uint16_t)i2s.rxDataWidth();
  uint16_t num_channels = (uint16_t)i2s.rxSlotMode();
  size_t rec_size = rec_microseconds / 1000.0 * ((sample_rate * (sample_width / 8)) * num_channels);
  *out_size = 0;

  log_d("Record WAV: rate:%lu, bits:%u, channels:%u, size:%lu", sample_rate, sample_width, num_channels, rec_size);

  uint8_t *wav_buf = ringPointer;
  ringPointer += rec_size;
  if (ringPointer > ringBuffer + RING_BUFFER_SIZE - rec_size)
    ringPointer = ringBuffer;
  if (wav_buf == NULL) {
    log_e("Failed to allocate WAV buffer with size %u", rec_size + WAVE_HEADER_SIZE);
    return NULL;
  }
  size_t wav_size = i2s.readBytes((char *)(wav_buf), rec_size);
  if (wav_size < rec_size) {
    log_e("Recorded %u bytes from %u", wav_size, rec_size);
  } else if (i2s.lastError()) {
    log_e("Read Failed! %d", i2s.lastError());
  } else {
    *out_size = rec_size;
    return wav_buf;
  }
  isRecording = false;
  return NULL;
}

size_t readAbitAndWrite(int rec_microseconds) {
  size_t buf_size;
  uint8_t *wav_buf = RecordMicroseconds(rec_microseconds, &buf_size);

  audio_block_t block;
  block.len = buf_size;
  block.data = wav_buf;

  if (block.len > 0) {
    xQueueSend(audioQueue, &block, portMAX_DELAY);
  }

  return buf_size;
}

void sdTask(void *arg) {
  audio_block_t block;
  std::vector<uint8_t> aacData;
  uint8_t _aacData[1024];

  while (sdTaskRunning) {

    if (xQueueReceive(audioQueue, &block, 12)) {
      // Write the audio data to the file
      aacData.clear();

      encoder.encode((int16_t *)block.data, block.len * 8 / i2s.rxDataWidth(), aacData);
      Serial.printf("PCM bytes in: %d  → AAC bytes out: %d\n",
                    block.len, aacData.size());
      memcpy(_aacData, aacData.data(), aacData.size());
      if (!file.write(_aacData, aacData.size()) && aacData.size() != 0) {
        Serial.println("Failed to write audio data to file!");
        isRecording = false;
      } else {
        Serial.printf("Recorded and Writed %d bytes.\n", aacData.size());
      }
    }
    // vTaskDelay(2 * portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}

void writeHeader() {
  // uint32_t sample_rate = i2s.rxSampleRate();
  // uint16_t sample_width = (uint16_t)i2s.rxDataWidth();
  // uint16_t num_channels = (uint16_t)i2s.rxSlotMode();
  // const pcm_wav_header_t wav_header = PCM_WAV_HEADER_DEFAULT(file.size() - PCM_WAV_HEADER_SIZE, sample_width, sample_rate, num_channels);
  // file.seek(0);
  // if (file.write((uint8_t *)&wav_header, PCM_WAV_HEADER_SIZE) != PCM_WAV_HEADER_SIZE) {
  //   Serial.println("Failed to write audio header to file!");
  //   isRecording = false;
  //   return;
  // }
  Serial.println("Recorded a AAC file!");
  file.close();
}

TaskHandle_t sdTaskHandle;

void loop() {
  if (recordToggleRequest) {
    recordToggleRequest = false;
    isRecording = !isRecording;
  }
  if (isRecording && !isRecording_last) {
    isRecording_last = true;
    ledState = LOW;
    digitalWrite(40, ledState);
    int serialNumber = getName(SD, "/", 0);
    if (serialNumber == -1)
      return;
    createWAV(serialNumber);
    sdTaskRunning = true;
    xTaskCreatePinnedToCore(sdTask, "sd",
                            24576, NULL, 1, &sdTaskHandle, 0);
  } else if (isRecording && isRecording_last) {
    ledState = !ledState;
    digitalWrite(40, ledState);
    readAbitAndWrite(64);
  } else if (!isRecording && isRecording_last) {
    ledState = HIGH;
    digitalWrite(40, ledState);
    isRecording_last = false;
    sdTaskRunning = false;
    while (eTaskGetState(sdTaskHandle) != eDeleted) {
      vTaskDelay(10);
    }
    writeHeader();
  }
}

void IRAM_ATTR btnCallBack() {
  if (((nowTime = micros()) - lastTime) >= 800) {
    recordToggleRequest = true;
    lastTime = nowTime;
  }
}
