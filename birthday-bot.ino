#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "all_frames.h"
#include "intro.h"
#include <DFRobotDFPlayerMini.h>

// ── Display & Pin Config ────────────────────────────────────
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define SCREEN_ADDRESS  0x3C
#define TOUCH_PIN       10

// ── Timing ──────────────────────────────────────────────────
#define FRAME_DELAY     42s
#define IDLE_TIMEOUT    4000
#define DOUBLE_TAP_GAP  350

// ── Frame Ranges ─────────────────────────────────────────────
#define IDLE_FRAME   151
#define BLINK_START  151
#define BLINK_MID    162
#define SMIRK_START  1
#define SMIRK_END    153
#define SNOOZE_START 154
#define SNOOZE_END   270
#define METER_START  271
#define METER_END    406
#define YAWN_START   407
#define YAWN_END     497
#define KISS_START   498
#define KISS_END     547
#define LASER_START  548
#define LASER_END    771

// ── Hardware ─────────────────────────────────────────────────
HardwareSerial mySerial(1);
DFRobotDFPlayerMini player;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Eye Animation State ───────────────────────────────────────
enum AnimState { ANIM_IDLE, ANIM_BLINK, ANIM_SEQUENCE, ANIM_KISS };
AnimState animState = ANIM_IDLE;

struct GifRange { int start; int end; };
const GifRange sequence[] = {
  { SMIRK_START,  SMIRK_END  },
  { SNOOZE_START, SNOOZE_END },
  { METER_START,  METER_END  },
  { YAWN_START,   YAWN_END   },
  { LASER_START,  LASER_END  }
};
const int SEQUENCE_COUNT = sizeof(sequence) / sizeof(sequence[0]);

int  seqIndex     = 0;
int  currentFrame = IDLE_FRAME;
bool blinkReverse = false;
bool justblinked  = false;

unsigned long lastFrameTime = 0;
unsigned long idleStartTime = 0;

// ── Intro State ───────────────────────────────────────────────
bool introFinished      = false;
int  introFrame         = 0;
unsigned long lastIntroFrameTime = 0;

// ── System Mode ───────────────────────────────────────────────
enum SystemMode { MODE_EYE_ANIMATIONS, MODE_BIRTHDAY };
SystemMode currentMode = MODE_EYE_ANIMATIONS;

// ── Birthday State ────────────────────────────────────────────
enum BirthSubState { BIRTH_INTRO, BIRTH_PLAYING, BIRTH_IDLE };
BirthSubState birthState = BIRTH_INTRO;

const char* familyNames[]          = { "Trevi", "Janina", "Troy", "Jericho", "Tyler", "Dad", "Jengga" };
int         familyMessageDuration[] = { 3000, 4000, 6000, 3000, 3000, 6000, 2000 };
int         familyMessageVolume[]   = { 25, 25, 15, 20, 15, 20, 25 };
const int   TOTAL_MESSAGES          = 7;
int         currentMessageIndex     = 0;

// Heart particles for birthday intro screen
struct Heart { int x; int y; int speed; };
Heart introHearts[10];

// Typewriter effect
unsigned long lastTypeTime  = 0;
int           typeCharCount = 0;
const char*   idleText      = "Tap for next...";

// ── Touch State ───────────────────────────────────────────────
bool lastTouchState = LOW;

// ── Helpers ──────────────────────────────────────────────────
void showFrame(int frameIndex) {
  display.clearDisplay();
  display.drawBitmap(0, 0, frames[frameIndex], FRAME_WIDTH, FRAME_HEIGHT, SSD1306_WHITE);
  display.display();
}

void drawHeart(int16_t x, int16_t y, int16_t size) {
  display.fillCircle(x - size/4, y - size/4, size/4, SSD1306_WHITE);
  display.fillCircle(x + size/4, y - size/4, size/4, SSD1306_WHITE);
  display.fillTriangle(x - size/2, y - size/4, x + size/2, y - size/4, x, y + size/2, SSD1306_WHITE);
}

void initIntroHearts() {
  for (int i = 0; i < 10; i++) {
    introHearts[i].x     = random(0, SCREEN_WIDTH);
    introHearts[i].y     = random(SCREEN_HEIGHT, SCREEN_HEIGHT + 40);
    introHearts[i].speed = random(1, 3);
  }
}

// ── Play a birthday message track (tracks start at folder 4) ─
void playBirthdayMessage(int index) {
  player.volume(familyMessageVolume[index]);
  player.playMp3Folder(index + 4);
  Serial.print("DFPlayer: playing track #");
  Serial.println(index + 1);
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);

  // 1. Init display immediately to suppress startup noise
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.display();

  // 2. DFPlayer needs ~2 s after power-on before it accepts commands
  delay(2000);

  // 3. Start DFPlayer on hardware serial pins 1 (RX) and 2 (TX)
  mySerial.begin(9600, SERIAL_8N1, 1, 2);
  if (!player.begin(mySerial)) {
    Serial.println("DFPlayer not detected. Check wiring on pins 1 and 2.");
    while (true);
  }
  Serial.println("DFPlayer ready.");

  // 4. Play boot jingle and show idle eye
  initIntroHearts();
  player.volume(10);
  player.playMp3Folder(1);
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {

  // ── 1. Boot intro animation ──────────────────────────────────
  if (!introFinished) {
    unsigned long now = millis();
    if (now - lastIntroFrameTime >= intro_delays[introFrame]) {
      lastIntroFrameTime = now;
      display.clearDisplay();
      for (uint16_t y = 0; y < INTRO_HEIGHT; y++) {
        for (uint16_t x = 0; x < INTRO_WIDTH; x++) {
          uint16_t byteIndex = y * ((INTRO_WIDTH + 7) / 8) + (x / 8);
          uint8_t  bitIndex  = 7 - (x % 8);
          if (intro_frames[introFrame][byteIndex] & (1 << bitIndex))
            display.drawPixel(x, y, SSD1306_WHITE);
        }
      }
      display.display();
      introFrame++;
      if (introFrame >= INTRO_FRAME_COUNT) {
        introFinished = true;
        display.clearDisplay();
        showFrame(IDLE_FRAME);
        animState     = ANIM_IDLE;
        idleStartTime = millis();
      }
    }
    return;
  }

  // ── 2. Touch: deferred single / double tap detection ─────────
  bool currentTouchState = digitalRead(TOUCH_PIN);
  bool singleTapTriggered = false;
  bool doubleTapTriggered = false;

  static unsigned long firstTapTime       = 0;
  static bool          waitingForSecondTap = false;
  static unsigned long lastDebounceTime    = 0;

  if (currentTouchState == HIGH && lastTouchState == LOW
      && (millis() - lastDebounceTime > 50)) {
    lastDebounceTime = millis();
    if (!waitingForSecondTap) {
      waitingForSecondTap = true;
      firstTapTime = millis();
    } else if (millis() - firstTapTime <= DOUBLE_TAP_GAP) {
      doubleTapTriggered  = true;
      waitingForSecondTap = false;
    }
  }
  lastTouchState = currentTouchState;

  if (waitingForSecondTap && (millis() - firstTapTime > DOUBLE_TAP_GAP)) {
    singleTapTriggered  = true;
    waitingForSecondTap = false;
  }

  // ── 3. Double-tap: switch system mode ────────────────────────
  if (doubleTapTriggered) {
    singleTapTriggered = false;
    if (currentMode == MODE_EYE_ANIMATIONS) {
      player.playMp3Folder(3);
      currentMode = MODE_BIRTHDAY;
      birthState  = BIRTH_INTRO;
      initIntroHearts();
    } else {
      currentMode = MODE_EYE_ANIMATIONS;
      player.stop();
      player.volume(10);
      player.playMp3Folder(2);
      animState     = ANIM_IDLE;
      idleStartTime = millis();
      showFrame(IDLE_FRAME);
    }
    return;
  }

  // ── 4. Mode runtime ──────────────────────────────────────────
  unsigned long now = millis();

  if (currentMode == MODE_EYE_ANIMATIONS) {

    // Single tap triggers kiss animation
    if (singleTapTriggered) {
      player.playMp3Folder(2);
      animState     = ANIM_KISS;
      currentFrame  = KISS_START;
      lastFrameTime = now;
    }

    switch (animState) {
      case ANIM_IDLE:
        if (now - idleStartTime >= IDLE_TIMEOUT) {
          lastFrameTime = now;
          if (!justblinked) {
            animState    = ANIM_BLINK;
            currentFrame = BLINK_START;
            blinkReverse = false;
          } else {
            animState    = ANIM_SEQUENCE;
            currentFrame = sequence[seqIndex].start;
          }
        }
        break;

      case ANIM_BLINK:
        if (now - lastFrameTime >= FRAME_DELAY) {
          lastFrameTime = now;
          showFrame(currentFrame);
          if (!blinkReverse) {
            if (currentFrame < BLINK_MID) {
              currentFrame++;
            } else {
              blinkReverse = true;
            }
          } else {
            if (currentFrame > BLINK_START) {
              currentFrame--;
            } else {
              justblinked   = true;
              animState     = ANIM_IDLE;
              idleStartTime = millis();
            }
          }
        }
        break;

      case ANIM_SEQUENCE:
        if (now - lastFrameTime >= FRAME_DELAY) {
          lastFrameTime = now;
          showFrame(currentFrame);
          if (currentFrame < sequence[seqIndex].end) {
            currentFrame++;
          } else {
            seqIndex      = (seqIndex + 1) % SEQUENCE_COUNT;
            justblinked   = false;
            animState     = ANIM_IDLE;
            idleStartTime = now;
            showFrame(IDLE_FRAME);
          }
        }
        break;

      case ANIM_KISS:
        if (now - lastFrameTime >= FRAME_DELAY) {
          lastFrameTime = now;
          showFrame(currentFrame);
          if (currentFrame < KISS_END) {
            currentFrame++;
          } else {
            animState     = ANIM_IDLE;
            idleStartTime = now;
            showFrame(IDLE_FRAME);
          }
        }
        break;
    }

  } else {
    // ── Birthday mode ─────────────────────────────────────────
    switch (birthState) {

      case BIRTH_INTRO:
        display.clearDisplay();
        for (int i = 0; i < 10; i++) {
          drawHeart(introHearts[i].x, introHearts[i].y, 6);
          introHearts[i].y -= introHearts[i].speed;
          if (introHearts[i].y < -10) {
            introHearts[i].y = SCREEN_HEIGHT + random(10, 30);
            introHearts[i].x = random(0, SCREEN_WIDTH);
          }
        }
        display.fillRect(4, 16, SCREEN_WIDTH - 8, 32, SSD1306_BLACK);
        display.drawRect(4, 16, SCREEN_WIDTH - 8, 32, SSD1306_WHITE);
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(14, 23);
        display.print("Tap for birthday");
        display.setCursor(35, 35);
        display.print("messages!");
        display.display();

        if (singleTapTriggered) {
          currentMessageIndex = 0;
          playBirthdayMessage(currentMessageIndex);
          birthState    = BIRTH_PLAYING;
          lastFrameTime = now;
        }
        break;

      case BIRTH_PLAYING: {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(5, 5);
        display.print("From: ");
        display.print(familyNames[currentMessageIndex]);

        int pulseSize = (now / 400) % 2 == 0 ? 10 : 16;
        drawHeart(SCREEN_WIDTH / 2, (SCREEN_HEIGHT / 2) + 4, pulseSize);

        display.setCursor(5, SCREEN_HEIGHT - 12);
        display.print("Message (");
        display.print(currentMessageIndex + 1);
        display.print("/");
        display.print(TOTAL_MESSAGES);
        display.print(")");
        display.display();

        // Advance when track duration expires or user taps
        // NOTE: replace with DFPlayer busy-pin check once wired
        if ((now - lastFrameTime >= familyMessageDuration[currentMessageIndex])
            || singleTapTriggered) {
          player.stop();
          birthState    = BIRTH_IDLE;
          typeCharCount = 0;
          lastTypeTime  = now;
        }
        break;
      }

      case BIRTH_IDLE:
        display.clearDisplay();

        // Simple pixel-art cake
        display.fillRect(54, 30, 20, 15, SSD1306_WHITE);
        display.fillRect(63, 22,  2,  8, SSD1306_WHITE);
        if ((now / 200) % 2 == 0) {
          display.drawPixel(63, 19, SSD1306_WHITE);
          display.drawPixel(64, 20, SSD1306_WHITE);
        } else {
          display.drawPixel(63, 20, SSD1306_WHITE);
        }

        // Typewriter
        if (now - lastTypeTime >= 50) {
          lastTypeTime = now;
          if (typeCharCount < (int)strlen(idleText)) typeCharCount++;
        }
        display.setCursor(5, SCREEN_HEIGHT - 12);
        for (int i = 0; i < typeCharCount; i++) display.print(idleText[i]);
        display.display();

        if (singleTapTriggered) {
          currentMessageIndex = (currentMessageIndex + 1) % TOTAL_MESSAGES;
          playBirthdayMessage(currentMessageIndex);
          birthState    = BIRTH_PLAYING;
          lastFrameTime = now;
        }
        break;
    }
  }
}
