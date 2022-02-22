#include <M5Core2.h>
#include <driver/i2s.h>

extern const int16_t piano_sample[128000];

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

enum SampleAdsr
{
  attack,
  decay,
  sustain,
  release,
};

struct Sample
{
  const int16_t *sample;
  uint32_t length;
  uint8_t root;
  uint32_t loop_start;
  uint32_t loop_end;

  uint32_t attack;
  uint32_t decay;
  float sustain;
  uint32_t release;
};

struct SamplePlayer
{
  SamplePlayer(struct Sample *sample, float pitch, float volume) : sample{sample}, pitch{pitch}, volume{volume} {}
  struct Sample *sample;
  float pitch;
  float volume;
  uint32_t pos = 0;
  float pos_f = 0.0f;
  bool playing = true;
  float adsr_gain = 0.0f;
  enum SampleAdsr adsr_state = SampleAdsr::attack;
};

struct Sample piano = Sample{
    piano_sample,
    128000,
    60,
    22971,
    23649,
    0,
    132300,
    0.1f,
    13230};

struct SamplePlayer samplePlayer = SamplePlayer{
    &piano,
    1.0f,
    1.0f};

void IRAM_ATTR AudioLoop()
{
  int16_t sampleDataU[SAMPLE_BUFFER_SIZE];

  if (samplePlayer.playing == false)
    return;

  // 波形を生成
  for (int n = 0; n < SAMPLE_BUFFER_SIZE; n++)
  {
    if (samplePlayer.pos >= samplePlayer.sample->length)
    {
      samplePlayer.playing = false;
      sampleDataU[n] = 0;
    }
    else
    {
      // 正弦波を生成
      sampleDataU[n] = samplePlayer.sample->sample[samplePlayer.pos];
      // sampleDataU[n] *= samplePlayer.volume;
    }

    samplePlayer.pos += 1;
  }

  static size_t bytes_written = 0;
  i2s_write(Speak_I2S_NUMBER, (const unsigned char *)sampleDataU, 2 * SAMPLE_BUFFER_SIZE, &bytes_written, portMAX_DELAY);
  printf("%d\n", bytes_written);
}

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

void SpeakInit(void)
{
  M5.Axp.SetSpkEnable(true);
  InitI2SSpeakOrMic(MODE_SPK);
}

void setup()
{
  M5.begin(true, true, true, true);
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(RED);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.printf("Sampler Test");
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setCursor(10, 26);
  M5.Lcd.printf("Touch to listen sin wave");
  SpeakInit();
  size_t bytes_written = 0;
  i2s_write(Speak_I2S_NUMBER, (const unsigned char *)piano_sample, 256000, &bytes_written, portMAX_DELAY);
  delay(100);

  // audioProcessTimer = timerBegin(0, 80, true);
  // timerAttachInterrupt(audioProcessTimer, &AudioLoop, true);
  // timerAlarmWrite(audioProcessTimer, 1000000, true);
  // timerAlarmEnable(audioProcessTimer);
}

void loop()
{
  TouchPoint_t pos = M5.Touch.getPressPoint();
  if (pos.y > 0)
  {
    AudioLoop();
  }
  else
    delay(10);
}
