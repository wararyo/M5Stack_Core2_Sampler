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
constexpr uint64_t AUDIO_LOOP_INTERVAL = (uint64_t)(SAMPLE_BUFFER_SIZE * 1000000 / SAMPLE_RATE);// micro seconds

#define MAX_SOUND 8 // 最大同時発音数

unsigned long nextAudioLoop = 0;

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
  SamplePlayer() : sample{nullptr}, pitch{1.0f}, volume{1.0f}, playing{false} {}
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

SamplePlayer players[MAX_SOUND] = {SamplePlayer()};

float PitchFromNoteNo(float noteNo, float root)
{
    float delta = noteNo - root;
    float f = ((pow(2.0f, delta / 12.0f)));   /* no frequency so dont use * 440.0f */
    return f;
}

void SendNoteOn(uint8_t noteNo, uint8_t velocity, uint8_t channnel) {
  for(uint8_t i = 0;i < MAX_SOUND;i++) {
    if(players[i].playing == false) {
      float pitch = PitchFromNoteNo(noteNo, piano.root);
      players[i] = SamplePlayer(&piano, pitch, velocity / 127.0f);
      return;
    }
  }
  // TODO: 全てのPlayerが再生中だった時には、最も昔に発音されたPlayerを停止する
}

void AudioLoop(void *pvParameters)
{
  while (true)
  {
    delay(1);
    if( nextAudioLoop > micros() ) {
      continue;
    }
    nextAudioLoop = micros() + AUDIO_LOOP_INTERVAL - 1000;

    int16_t sampleDataU[SAMPLE_BUFFER_SIZE] = {0x0000};

    // 波形を生成
    for (uint8_t i = 0; i < MAX_SOUND; i++)
    {
      SamplePlayer *player = &players[i];
      if(player->playing == false) continue;
      for (int n = 0; n < SAMPLE_BUFFER_SIZE; n++)
      {
        if (player->pos >= player->sample->length)
        {
          player->playing = false;
          break;
        }
        else
        {
          // 波形を読み込む
          float sample = player->sample->sample[player->pos];
          sample *= player->volume;
          sampleDataU[n] += sample;

          // 次のサンプルへ移動
          int32_t pitch_u = player->pitch;
          player->pos_f += player->pitch - pitch_u; /* does not work great when pos_f is bigger */
          player->pos += pitch_u;
          int posI = player->pos_f;
          player->pos += posI;
          player->pos_f -= posI;
        }
      }
    }

    static size_t bytes_written = 0;
    i2s_write(Speak_I2S_NUMBER, (const unsigned char *)sampleDataU, 2 * SAMPLE_BUFFER_SIZE, &bytes_written, portMAX_DELAY);
  }
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
  M5.Lcd.printf("Touch to play piano");
  SpeakInit();
  size_t bytes_written = 0;
  i2s_write(Speak_I2S_NUMBER, (const unsigned char *)piano_sample, 256000, &bytes_written, portMAX_DELAY);
  delay(100);

  // Core0でタスク起動
  xTaskCreateUniversal(
      AudioLoop,
      "audioLoop",
      8192,
      NULL,
      1,
      NULL,
      0);
  // ウォッチドッグ停止
  disableCore0WDT();
}

void loop()
{
  static unsigned char noteNo = 60;
  TouchPoint_t pos = M5.Touch.getPressPoint();
  if (pos.y > 0)
  {
    SendNoteOn(noteNo,80,1);
    noteNo++;
    delay(500);
  }
  else
    delay(10);
}
