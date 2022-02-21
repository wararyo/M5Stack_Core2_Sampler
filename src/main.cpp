#include <M5Core2.h>
#include <driver/i2s.h>

extern const unsigned char previewR[396900];

#define CONFIG_I2S_BCK_PIN 12
#define CONFIG_I2S_LRCK_PIN 0
#define CONFIG_I2S_DATA_PIN 2
#define CONFIG_I2S_DATA_IN_PIN 34

#define Speak_I2S_NUMBER I2S_NUM_0

#define MODE_MIC 0
#define MODE_SPK 1
#define DATA_SIZE 1024

#define SAMPLE_BUFFER_SIZE 64
#define SAMPLE_RATE 44100
constexpr uint64_t TIMER_INTERVAL = (uint64_t)(SAMPLE_BUFFER_SIZE * 1000000 / SAMPLE_RATE);

hw_timer_t *audioProcessTimer = NULL;

bool InitI2SSpeakOrMic(int mode)
{
  esp_err_t err = ESP_OK;

  i2s_driver_uninstall(Speak_I2S_NUMBER);
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 2,
      .dma_buf_len = 128,
  };
  if (mode == MODE_MIC)
  {
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
  }
  else
  {
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = true;
  }
  err += i2s_driver_install(Speak_I2S_NUMBER, &i2s_config, 0, NULL);
  i2s_pin_config_t tx_pin_config;

  tx_pin_config.bck_io_num = CONFIG_I2S_BCK_PIN;
  tx_pin_config.ws_io_num = CONFIG_I2S_LRCK_PIN;
  tx_pin_config.data_out_num = CONFIG_I2S_DATA_PIN;
  tx_pin_config.data_in_num = CONFIG_I2S_DATA_IN_PIN;
  err += i2s_set_pin(Speak_I2S_NUMBER, &tx_pin_config);
  // err += i2s_set_clk(Speak_I2S_NUMBER, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);

  return true;
}

void DisplayInit(void)
{
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(2);
}

void SpeakInit(void)
{
  M5.Axp.SetSpkEnable(true);
  InitI2SSpeakOrMic(MODE_SPK);
}

void DingDong(void)
{
  size_t bytes_written = 0;
  i2s_write(Speak_I2S_NUMBER, previewR, 396900, &bytes_written, portMAX_DELAY);
}

void IRAM_ATTR AudioLoop()
{
  int16_t sampleDataU[SAMPLE_BUFFER_SIZE];

  for (int n = 0; n < SAMPLE_BUFFER_SIZE; n++)
  {
    // 正弦波を生成
    sampleDataU[n] = int16_t(sin(n * 2.0f * PI / 64.0f) * 32767.0f); //64サンプルで1周期
  }

  static size_t bytes_written = 0;
  i2s_write(Speak_I2S_NUMBER, (const unsigned char *)sampleDataU, 2 * SAMPLE_BUFFER_SIZE, &bytes_written, portMAX_DELAY);
  printf("%d\n", bytes_written);
}

void setup()
{
  M5.begin(true, true, true, true);
  DisplayInit();
  M5.Lcd.setTextColor(RED);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.printf("Sampler Test");
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setCursor(10, 26);
  M5.Lcd.printf("Touch to listen sin wave");
  SpeakInit();
  DingDong();
  delay(100);

  // audioProcessTimer = timerBegin(0, 80, true);
  // timerAttachInterrupt(audioProcessTimer, &AudioLoop, true);
  // timerAlarmWrite(audioProcessTimer, 1000000, true);
  // timerAlarmEnable(audioProcessTimer);
}

void loop()
{
  TouchPoint_t pos = M5.Touch.getPressPoint();
  if (pos.y > 0) {
    AudioLoop();
  }
  else delay(10);
}
