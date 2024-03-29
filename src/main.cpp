#include <M5Unified.h>
#include <driver/i2s.h>
#include <ml_reverb.h>

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
constexpr uint32_t AUDIO_LOOP_INTERVAL = (uint32_t)(SAMPLE_BUFFER_SIZE * 1000000 / SAMPLE_RATE);// micro seconds

#define MAX_SOUND 12 // 最大同時発音数

float masterVolume = 0.5f;

unsigned long nextAudioLoop = 0;
uint32_t audioProcessTime = 0; // プロファイリング用 一回のオーディオ処理にかかる時間

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
  uint32_t loopStart;
  uint32_t loopEnd;

  bool adsrEnabled;
  float attack;
  float decay;
  float sustain;
  float release;
};

struct SamplePlayer
{
  SamplePlayer(struct Sample *sample, uint8_t noteNo, float volume)
    : sample{sample}, noteNo{noteNo}, volume{volume}, createdAt{micros()} {}
  SamplePlayer() : sample{nullptr}, noteNo{60}, volume{1.0f}, playing{false}, createdAt{micros()} {}
  struct Sample *sample;
  uint8_t noteNo;
  float pitchBend = 0;
  float volume;
  unsigned long createdAt = 0;
  uint32_t pos = 0;
  float pos_f = 0.0f;
  bool playing = true;
  bool released = false;
  float adsrGain = 0.0f;
  enum SampleAdsr adsrState = SampleAdsr::attack;
};

struct Sample piano = Sample{
    piano_sample,
    128000,
    60,
    24120,
    24288,
    true,
    1.0f,
    0.998887f,
    0.1f,
    0.988885f};

SamplePlayer players[MAX_SOUND] = {SamplePlayer()};

inline float PitchFromNoteNo(float noteNo, float root)
{
    float delta = noteNo - root;
    float f = ((pow(2.0f, delta / 12.0f)));
    return f;
}

inline void UpdateAdsr(SamplePlayer *player)
{
  Sample *sample = player->sample;
  if(player->released) player->adsrState = release;

  switch (player->adsrState)
  {
  case attack:
    player->adsrGain += sample->attack;
    if (player->adsrGain >= 1.0f)
    {
      player->adsrGain = 1.0f;
      player->adsrState = decay;
    }
    break;
  case decay:
    player->adsrGain = (player->adsrGain - sample->sustain) * sample->decay + sample->sustain;
    if ((player->adsrGain - sample->sustain) < 0.01f)
    {
      player->adsrState = sustain;
      player->adsrGain = sample->sustain;
    }
    break;
  case sustain:
    break;
  case release:
    player->adsrGain *= sample->release;
    if (player->adsrGain < 0.01f)
    {
      player->adsrGain = 0;
      player->playing = false;
    }
    break;
  }
}

void SendNoteOn(uint8_t noteNo, uint8_t velocity, uint8_t channnel) {
  uint8_t oldestPlayerId = 0;
  for(uint8_t i = 0;i < MAX_SOUND;i++) {
    if(players[i].playing == false) {
      players[i] = SamplePlayer(&piano, noteNo, velocity / 127.0f);
      return;
    } else {
      if(players[i].createdAt < players[oldestPlayerId].createdAt) oldestPlayerId = i;
    }
  }
  // 全てのPlayerが再生中だった時には、最も昔に発音されたPlayerを停止する
  players[oldestPlayerId] = SamplePlayer(&piano, noteNo, velocity / 127.0f);
}
void SendNoteOff(uint8_t noteNo,  uint8_t velocity, uint8_t channnel) {
  for(uint8_t i = 0;i < MAX_SOUND;i++) {
    if(players[i].playing == true && players[i].noteNo == noteNo) {
      players[i].released = true;
    }
  }
}

void AudioLoop(void *pvParameters)
{
  while (true)
  {
    float data[SAMPLE_BUFFER_SIZE] = {0.0f};

    unsigned long startTime = micros();

    // 波形を生成
    for (uint8_t i = 0; i < MAX_SOUND; i++)
    {
      SamplePlayer *player = &players[i];
      if(player->playing == false) continue;
      Sample *sample = player->sample;
      if(sample->adsrEnabled) UpdateAdsr(player);
      if(player->playing == false) continue;

      float pitch = PitchFromNoteNo(player->noteNo, player->sample->root);

      for (int n = 0; n < SAMPLE_BUFFER_SIZE; n++)
      {
        if (player->pos >= sample->length)
        {
          player->playing = false;
          break;
        }
        else
        {
          // 波形を読み込む
          float val = sample->sample[player->pos];
          if(sample->adsrEnabled) val *= player->adsrGain;
          val *= player->volume;
          data[n] += val;

          // 次のサンプルへ移動
          int32_t pitch_u = pitch;
          player->pos_f += pitch - pitch_u;
          player->pos += pitch_u;
          int posI = player->pos_f;
          player->pos += posI;
          player->pos_f -= posI;

          // ループポイントが設定されている場合はループする
          if(sample->adsrEnabled && player->released == false && player->pos >= sample->loopEnd)
            player->pos -= (sample->loopEnd - sample->loopStart);
        }
      }
    }

    Reverb_Process(data, SAMPLE_BUFFER_SIZE);

    int16_t dataI[SAMPLE_BUFFER_SIZE];
    for (uint8_t i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
      dataI[i] = int16_t(data[i] * masterVolume);
    }

    unsigned long endTime = micros();
    audioProcessTime = endTime - startTime;

    static size_t bytes_written = 0;
    i2s_write(Speak_I2S_NUMBER, (const unsigned char *)dataI, 2 * SAMPLE_BUFFER_SIZE, &bytes_written, portMAX_DELAY);
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
      .dma_buf_count = 8,
      .dma_buf_len = 64,
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

  return true;
}

// 動作確認用機能のため、CH1のみに対応
void HandleMidiMessage(uint8_t *message)
{
  if (message[0] == 0x90)
  {
    SendNoteOn(message[1], message[2], 1);
  }
  else if (message[0] == 0x80)
  {
    SendNoteOff(message[1], message[2], 1);
  }
}

void setup()
{
  M5.begin();
  M5.Display.startWrite();
  M5.Display.fillScreen(WHITE);
  M5.Display.setTextColor(BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(RED);
  M5.Display.setCursor(10, 10);
  M5.Display.printf("Sampler Test");
  M5.Display.setTextColor(BLACK);
  M5.Display.setCursor(10, 32);
  M5.Display.printf("Input serial MIDI");
  M5.Display.setCursor(10, 48);
  M5.Display.printf("to play the piano.");
  M5.Display.setCursor(10, 80);
  M5.Display.printf("Audio load");
  M5.Display.setCursor(64, 216);
  M5.Display.printf("Do     Mi     So");
  M5.Display.endWrite();
  InitI2SSpeakOrMic(MODE_SPK);

  size_t bytes_written = 0;
  i2s_write(Speak_I2S_NUMBER, (const unsigned char *)piano_sample, 256000, &bytes_written, portMAX_DELAY);
  delay(100);

  static float revBuffer[REV_BUFF_SIZE];
  Reverb_Setup(revBuffer);
  Reverb_SetLevel(0, 0.2f);

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
  // シリアルポートから受信したMIDIを再生
  static uint8_t message[3] = {0x00};
  while (Serial.available() > 0)
  {
    uint8_t byte = Serial.read();
    if(message[0] != 0x00) {
      if(message[1] == 0x00) message[1] = byte;
      else if(message[2] == 0x00) {
        message[2] = byte;
        HandleMidiMessage(message);
        message[0] = 0x00;
        message[1] = 0x00;
        message[2] = 0x00;
      }
    }
    else if (byte == 0x90 || byte == 0x80)
    {
      message[0] = byte;
    }
  }

  // 本体ボタンタッチで単音を再生
  M5.update();
  if(M5.BtnA.wasPressed()) {
    SendNoteOn(60, 100, 1);
  }
  else if(M5.BtnA.wasReleased()) {
    SendNoteOff(60, 100, 1);
  }
  if(M5.BtnB.wasPressed()) {
    SendNoteOn(64, 100, 1);
  }
  else if(M5.BtnB.wasReleased()) {
    SendNoteOff(64, 100, 1);
  }
    if(M5.BtnC.wasPressed()) {
    SendNoteOn(67, 100, 1);
  }
  else if(M5.BtnC.wasReleased()) {
    SendNoteOff(67, 100, 1);
  }

  // オーディオ負荷率を出力
  M5.Display.startWrite();
  M5.Display.fillRect(10,96,310,16,WHITE);
  M5.Display.drawRect(10,96,240,16,BLACK);
  float audioLoad = (float)audioProcessTime / AUDIO_LOOP_INTERVAL;
  M5.Display.fillRect(10,96,audioLoad * 240,16,BLUE);
  M5.Display.endWrite();

  delay(30);
}
