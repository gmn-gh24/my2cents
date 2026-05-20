/*
 * ESP32C3_042_OLED_Animation
 * SPDX-License-Identifier: MIT
 *
 * Author: Gonzalo MB <gon_mb@hotmail.com>
 * Board: ESP32-C3 Supermini / 0.42" OLED (72x40 visible area)
 *
 * Overview:
 *   Multi-phase demo for the onboard OLED:
 *   1) Large scrolling text
 *   2) Lightweight autoplay "Space Invaders"-style scene
 *   3) Pac-Man chase scene (right-to-left, then left-to-right)
 *   4) Calibration outline hold, then loop
 *
 * Display notes:
 *   This panel behaves like a 72x40 active window within a 128x64 framebuffer.
 *   The calibrated visible area for this board is:
 *     ACTIVE_X=28, ACTIVE_Y=24, ACTIVE_W=72, ACTIVE_H=40
 *
 * Compile:
 *   arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashSize=4M \
 *     --libraries sketches/libraries sketches/ESP32C3_042_OLED_Animation
 *
 * Upload:
 *   arduino-cli upload -p COM20 \
 *     --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashSize=4M \
 *     sketches/ESP32C3_042_OLED_Animation
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_system.h>

constexpr uint8_t PIN_OLED_SDA = 5;
constexpr uint8_t PIN_OLED_SCL = 6;
constexpr uint8_t OLED_ADDRS[] = {0x3C, 0x3D};
constexpr int DISPLAY_W = 128;
constexpr int DISPLAY_H = 64;

// ---------- User-tunable config ----------
// Calibrated visible window for this specific 0.42" board variant.
constexpr int ACTIVE_X = 28;
constexpr int ACTIVE_Y = 24;
constexpr int ACTIVE_W = 72;
constexpr int ACTIVE_H = 40;

// Debug toggles for display calibration.
constexpr bool CALIBRATION_ONLY_MODE = false;   // true: show outline only
constexpr bool SHOW_CALIBRATION_GUIDE = false;  // true: overlay outline on animation

// Render cadence (~30 FPS).
constexpr uint32_t FRAME_MS = 33;

Adafruit_SSD1306 display(DISPLAY_W, DISPLAY_H, &Wire, -1);

static uint8_t oledAddr = 0;
static uint32_t lastFrameMs = 0;
static uint32_t frameCount = 0;

enum DemoPhase : uint8_t {
  PHASE_SCROLL_TEXT,
  PHASE_SPACE_INVADERS,
  PHASE_PACMAN_RTL,
  PHASE_PACMAN_LTR,
  PHASE_CALIBRATION_HOLD,
  PHASE_FILL_BAR
};
static DemoPhase phase = PHASE_SCROLL_TEXT;
static uint32_t phaseStartMs = 0;

// Phase 1: scrolling text banner.
static const char *SCROLL_TEXT = "See my arduino sketch here: https://github.com/gmn-gh24/my2cents";
constexpr uint8_t SCROLL_TEXT_SIZE = 3;
constexpr int8_t SCROLL_STEP_PX = 2;
constexpr uint8_t SCROLL_PASSES_BEFORE_GAME = 1;
static int16_t scrollX = ACTIVE_X + ACTIVE_W;
static int16_t scrollTextWidth = 0;
static uint8_t scrollPasses = 0;

// Phase 2: compact autoplay Space Invaders-inspired scene.
constexpr uint32_t SPACE_INVADERS_DURATION_MS = 12000;
constexpr uint8_t INV_ROWS = 2;
constexpr uint8_t INV_COLS = 5;
constexpr int16_t INV_W = 6;
constexpr int16_t INV_H = 4;
constexpr int16_t INV_SPX = 7;
constexpr int16_t INV_SPY = 6;
constexpr int16_t PLAYER_W = 7;
constexpr int16_t PLAYER_H = 3;

static bool invAlive[INV_ROWS][INV_COLS];
static int16_t formationX = ACTIVE_X + 4;
static int16_t formationY = ACTIVE_Y + 4;
static int8_t formationDir = 1;
static uint8_t formationTick = 0;
static bool invaderAnim = false;

static int16_t playerX = ACTIVE_X + (ACTIVE_W / 2) - (PLAYER_W / 2);
static int8_t playerDir = 1;

static bool playerShotActive = false;
static int16_t playerShotX = 0;
static int16_t playerShotY = 0;

static bool enemyShotActive = false;
static int16_t enemyShotX = 0;
static int16_t enemyShotY = 0;

// Brief white flash when something gets hit.
static uint8_t hitFlashFrames = 0;

// Phase 3: Pac-Man chase scene.
constexpr uint32_t CALIBRATION_HOLD_MS = 5000;
constexpr uint32_t FILL_STEP_MS = 20;
constexpr int8_t PACMAN_DIR_LEFT = -1;
constexpr int8_t PACMAN_DIR_RIGHT = 1;
constexpr int16_t PACMAN_R = 11;
constexpr int16_t PACMAN_MOUTH_HALF_H = 7;
constexpr int16_t PACMAN_SPEED = 2;
constexpr int16_t PACMAN_Y = ACTIVE_Y + (ACTIVE_H / 2);

constexpr uint8_t PAC_GHOST_COUNT = 3;
constexpr int16_t PAC_GHOST_W = 18;
constexpr int16_t PAC_GHOST_H = 18;
constexpr int16_t PAC_GHOST_GAP = 20;

static int16_t pacmanX = 0;
static int8_t pacmanDir = PACMAN_DIR_LEFT;
static bool pacmanMouthOpen = true;
static bool ghostLegsAlt = false;
static uint8_t pacmanAnimTick = 0;
static int16_t fillWidth = 0;
static uint32_t lastFillStepMs = 0;

// Draw the calibrated active-area rectangle for alignment/debug.
static void drawCalibrationOutline(uint16_t color = SSD1306_WHITE) {
  display.drawRect(ACTIVE_X, ACTIVE_Y, ACTIVE_W, ACTIVE_H, color);
}

// Print all I2C responders to Serial for troubleshooting.
static void scanI2c() {
  Serial.println("I2C scan:");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X\n", addr);
    }
  }
}

// Probe one I2C address and return true if a device acknowledges.
static bool oledPresentAt(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Set demo phase and keep a common phase start timestamp.
static void setPhase(DemoPhase nextPhase) {
  phase = nextPhase;
  phaseStartMs = millis();
}

// Reset Space Invaders state to a fresh autoplay round.
static void resetInvaders() {
  for (uint8_t r = 0; r < INV_ROWS; r++) {
    for (uint8_t c = 0; c < INV_COLS; c++) {
      invAlive[r][c] = true;
    }
  }
  formationX = ACTIVE_X + 4;
  formationY = ACTIVE_Y + 4;
  formationDir = 1;
  formationTick = 0;
  invaderAnim = false;

  playerX = ACTIVE_X + (ACTIVE_W / 2) - (PLAYER_W / 2);
  playerDir = 1;
  playerShotActive = false;
  enemyShotActive = false;
}

// Check if all invaders have been removed.
static bool allInvadersDead() {
  for (uint8_t r = 0; r < INV_ROWS; r++) {
    for (uint8_t c = 0; c < INV_COLS; c++) {
      if (invAlive[r][c]) return false;
    }
  }
  return true;
}

// Switch from text phase to game phase.
static void startSpaceGame() {
  setPhase(PHASE_SPACE_INVADERS);
  hitFlashFrames = 0;
  resetInvaders();
}

// Initialize a Pac-Man chase pass. dir=-1 for right->left, dir=+1 for left->right.
static void initPacmanPass(int8_t dir) {
  pacmanDir = dir;
  pacmanMouthOpen = true;
  ghostLegsAlt = false;
  pacmanAnimTick = 0;

  if (dir == PACMAN_DIR_LEFT) {
    pacmanX = ACTIVE_X + ACTIVE_W + PACMAN_R + 2;
  } else {
    pacmanX = ACTIVE_X - PACMAN_R - 2;
  }
}

// Render and advance the scrolling text phase.
static void drawPhaseScrollText() {
  display.setTextSize(SCROLL_TEXT_SIZE);
  display.setTextColor(SSD1306_WHITE);
  const int16_t textY = ACTIVE_Y + ((ACTIVE_H - (int16_t)(SCROLL_TEXT_SIZE * 8)) / 2);
  display.setCursor(scrollX, textY);
  display.print(SCROLL_TEXT);

  scrollX -= SCROLL_STEP_PX;
  if (scrollX + scrollTextWidth < ACTIVE_X) {
    scrollX = ACTIVE_X + ACTIVE_W;
    scrollPasses++;
    if (scrollPasses >= SCROLL_PASSES_BEFORE_GAME) {
      startSpaceGame();
    }
  }
}

// Draw a tiny invader sprite.
static void drawInvader(int16_t x, int16_t y, bool animFrame) {
  display.drawPixel(x + 1, y, SSD1306_WHITE);
  display.drawPixel(x + 4, y, SSD1306_WHITE);
  display.drawFastHLine(x, y + 1, INV_W, SSD1306_WHITE);
  display.drawFastHLine(x + 1, y + 2, INV_W - 2, SSD1306_WHITE);
  if (animFrame) {
    display.drawPixel(x, y + 3, SSD1306_WHITE);
    display.drawPixel(x + 5, y + 3, SSD1306_WHITE);
  } else {
    display.drawPixel(x + 1, y + 3, SSD1306_WHITE);
    display.drawPixel(x + 4, y + 3, SSD1306_WHITE);
  }
}

// Draw Pac-Man as a filled disc with an animated mouth wedge.
static void drawPacman(int16_t cx, int16_t cy, int8_t dir, bool mouthOpen) {
  display.fillCircle(cx, cy, PACMAN_R, SSD1306_WHITE);

  if (mouthOpen) {
    const int16_t tipX = cx + dir * (PACMAN_R + 1);
    display.fillTriangle(cx, cy, tipX, cy - PACMAN_MOUTH_HALF_H, tipX, cy + PACMAN_MOUTH_HALF_H, SSD1306_BLACK);
  } else {
    // Closed-mouth frame keeps a subtle notch so the animation still reads as "chomp".
    const int16_t tipX = cx + dir * (PACMAN_R + 1);
    display.fillTriangle(cx, cy, tipX, cy - 1, tipX, cy + 1, SSD1306_BLACK);
  }

  // Eye placement matches travel direction.
  display.drawPixel(cx + dir * 2, cy - 3, SSD1306_BLACK);
}

// Draw a classic-style ghost silhouette with eyes/pupils and animated feet.
static void drawGhost(int16_t x, int16_t y, int8_t lookDir, bool legsAlt) {
  // Body: rounded cap + rectangular lower body.
  const int16_t capR = 6;
  const int16_t capY = y + capR;
  display.fillCircle(x + capR, capY, capR, SSD1306_WHITE);
  display.fillCircle(x + PAC_GHOST_W - 1 - capR, capY, capR, SSD1306_WHITE);
  display.fillRect(x + capR, y, PAC_GHOST_W - (2 * capR), capR + 1, SSD1306_WHITE);
  display.fillRect(x, y + capR, PAC_GHOST_W, PAC_GHOST_H - capR, SSD1306_WHITE);

  // Scalloped feet (alternate frame gives "walking" illusion).
  const int16_t footY = y + PAC_GHOST_H - 1;
  const int16_t footStart = legsAlt ? 2 : 1;
  for (int16_t fx = footStart; fx < PAC_GHOST_W - 1; fx += 3) {
    display.drawPixel(x + fx, footY, SSD1306_BLACK);
  }

  // Eyes are carved in black, then white pupils are drawn on top.
  constexpr int16_t eyeW = 5;
  constexpr int16_t eyeH = 6;
  constexpr int16_t eyeGap = 2;
  const int16_t eyeY = y + 6;
  const int16_t eyeX1 = x + ((PAC_GHOST_W - (2 * eyeW + eyeGap)) / 2);
  const int16_t eyeX2 = eyeX1 + eyeW + eyeGap;

  display.fillRect(eyeX1, eyeY, eyeW, eyeH, SSD1306_BLACK);
  display.fillRect(eyeX2, eyeY, eyeW, eyeH, SSD1306_BLACK);

  // Pupils looking in movement direction.
  const int16_t pupilOffset = (lookDir == PACMAN_DIR_LEFT) ? -1 : 1;
  display.fillRect(eyeX1 + 1 + pupilOffset, eyeY + 1, 2, 3, SSD1306_WHITE);
  display.fillRect(eyeX2 + 1 + pupilOffset, eyeY + 1, 2, 3, SSD1306_WHITE);
}

// Draw the player ship near the bottom of the active area.
static void drawPlayer() {
  const int16_t y = ACTIVE_Y + ACTIVE_H - 3;
  display.drawFastHLine(playerX, y, PLAYER_W, SSD1306_WHITE);
  display.drawFastHLine(playerX + 1, y - 1, PLAYER_W - 2, SSD1306_WHITE);
  display.drawPixel(playerX + (PLAYER_W / 2), y - 2, SSD1306_WHITE);
}

// Randomly spawn enemy projectiles from currently alive invaders.
static void spawnEnemyShotIfNeeded() {
  if (enemyShotActive) return;
  if (random(0, 100) > 8) return;

  int8_t candidateRows[INV_ROWS * INV_COLS];
  int8_t candidateCols[INV_ROWS * INV_COLS];
  uint8_t count = 0;
  for (uint8_t r = 0; r < INV_ROWS; r++) {
    for (uint8_t c = 0; c < INV_COLS; c++) {
      if (!invAlive[r][c]) continue;
      candidateRows[count] = (int8_t)r;
      candidateCols[count] = (int8_t)c;
      count++;
    }
  }
  if (count == 0) return;

  const uint8_t pick = (uint8_t)random(0, count);
  const int16_t ex = formationX + candidateCols[pick] * INV_SPX + (INV_W / 2);
  const int16_t ey = formationY + candidateRows[pick] * INV_SPY + INV_H + 1;
  enemyShotX = ex;
  enemyShotY = ey;
  enemyShotActive = true;
}

// Update game physics, movement, and collision state.
static void updateSpaceGame() {
  formationTick++;
  if (formationTick >= 5) {
    formationTick = 0;
    invaderAnim = !invaderAnim;

    formationX += formationDir;

    int16_t minX = 1000;
    int16_t maxX = -1000;
    for (uint8_t r = 0; r < INV_ROWS; r++) {
      for (uint8_t c = 0; c < INV_COLS; c++) {
        if (!invAlive[r][c]) continue;
        const int16_t ix = formationX + c * INV_SPX;
        if (ix < minX) minX = ix;
        if (ix + INV_W > maxX) maxX = ix + INV_W;
      }
    }

    if (minX <= ACTIVE_X + 1 || maxX >= ACTIVE_X + ACTIVE_W - 1) {
      formationDir = -formationDir;
      formationX += formationDir;
      formationY += 2;
    }
  }

  playerX += playerDir;
  const int16_t playerMin = ACTIVE_X + 2;
  const int16_t playerMax = ACTIVE_X + ACTIVE_W - PLAYER_W - 2;
  if (playerX <= playerMin || playerX >= playerMax) {
    playerDir = -playerDir;
    playerX += playerDir;
  }

  if (!playerShotActive && random(0, 100) < 14) {
    playerShotX = playerX + (PLAYER_W / 2);
    playerShotY = ACTIVE_Y + ACTIVE_H - 5;
    playerShotActive = true;
  }

  if (playerShotActive) {
    playerShotY -= 2;
    if (playerShotY < ACTIVE_Y + 1) {
      playerShotActive = false;
    } else {
      for (uint8_t r = 0; r < INV_ROWS; r++) {
        for (uint8_t c = 0; c < INV_COLS; c++) {
          if (!invAlive[r][c]) continue;
          const int16_t ix = formationX + c * INV_SPX;
          const int16_t iy = formationY + r * INV_SPY;
          if (playerShotX >= ix && playerShotX <= ix + INV_W &&
              playerShotY >= iy && playerShotY <= iy + INV_H) {
            invAlive[r][c] = false;
            playerShotActive = false;
            hitFlashFrames = 2;
            break;
          }
        }
      }
    }
  }

  spawnEnemyShotIfNeeded();
  if (enemyShotActive) {
    enemyShotY += 2;
    if (enemyShotY > ACTIVE_Y + ACTIVE_H - 1) {
      enemyShotActive = false;
    } else {
      const int16_t py = ACTIVE_Y + ACTIVE_H - 3;
      if (enemyShotX >= playerX && enemyShotX <= playerX + PLAYER_W &&
          enemyShotY >= py - 2 && enemyShotY <= py + 1) {
        enemyShotActive = false;
        hitFlashFrames = 3;
      }
    }
  }

  if (allInvadersDead()) {
    resetInvaders();
  }
}

// Draw current game frame from the latest state.
static void drawSpaceGame() {
  if (hitFlashFrames > 0) {
    display.fillRect(ACTIVE_X + 1, ACTIVE_Y + 1, ACTIVE_W - 2, ACTIVE_H - 2, SSD1306_WHITE);
    hitFlashFrames--;
    return;
  }

  // Tiny starfield for motion.
  for (uint8_t i = 0; i < 7; i++) {
    const int16_t sx = ACTIVE_X + (int16_t)((i * 11 + frameCount) % (uint32_t)ACTIVE_W);
    const int16_t sy = ACTIVE_Y + (int16_t)((i * 5 + (frameCount / 3)) % (uint32_t)ACTIVE_H);
    display.drawPixel(sx, sy, SSD1306_WHITE);
  }

  for (uint8_t r = 0; r < INV_ROWS; r++) {
    for (uint8_t c = 0; c < INV_COLS; c++) {
      if (!invAlive[r][c]) continue;
      const int16_t ix = formationX + c * INV_SPX;
      const int16_t iy = formationY + r * INV_SPY;
      drawInvader(ix, iy, invaderAnim);
    }
  }

  drawPlayer();

  if (playerShotActive) {
    display.drawFastVLine(playerShotX, playerShotY - 2, 3, SSD1306_WHITE);
  }
  if (enemyShotActive) {
    display.drawFastVLine(enemyShotX, enemyShotY, 2, SSD1306_WHITE);
  }
}

// Update Pac-Man scene animation and advance phase when run exits screen.
static void updatePacmanScene() {
  pacmanX += pacmanDir * PACMAN_SPEED;

  pacmanAnimTick++;
  if (pacmanAnimTick >= 4) {
    pacmanAnimTick = 0;
    pacmanMouthOpen = !pacmanMouthOpen;
    ghostLegsAlt = !ghostLegsAlt;
  }

  bool passComplete = false;
  if (pacmanDir == PACMAN_DIR_LEFT) {
    const int16_t tailGhostRight = pacmanX + PAC_GHOST_COUNT * PAC_GHOST_GAP + PAC_GHOST_W;
    passComplete = (tailGhostRight < ACTIVE_X - 1);
  } else {
    const int16_t tailGhostLeft = pacmanX - PAC_GHOST_COUNT * PAC_GHOST_GAP - PAC_GHOST_W;
    passComplete = (tailGhostLeft > ACTIVE_X + ACTIVE_W + 1);
  }

  if (!passComplete) return;

  if (phase == PHASE_PACMAN_RTL) {
    setPhase(PHASE_PACMAN_LTR);
    initPacmanPass(PACMAN_DIR_RIGHT);
  } else if (phase == PHASE_PACMAN_LTR) {
    setPhase(PHASE_CALIBRATION_HOLD);
  }
}

// Draw Pac-Man + 3 ghosts in a chase line.
static void drawPacmanScene() {
  drawPacman(pacmanX, PACMAN_Y, pacmanDir, pacmanMouthOpen);

  for (uint8_t i = 0; i < PAC_GHOST_COUNT; i++) {
    const int16_t spacing = (int16_t)(i + 1) * PAC_GHOST_GAP;
    int16_t ghostX;
    if (pacmanDir == PACMAN_DIR_LEFT) {
      ghostX = pacmanX + spacing;
    } else {
      ghostX = pacmanX - spacing - PAC_GHOST_W;
    }

    // Stagger feet animation across ghosts to add motion variation.
    const bool ghostFeetFrame = ((i % 2) == 0) ? ghostLegsAlt : !ghostLegsAlt;
    drawGhost(ghostX, PACMAN_Y - (PAC_GHOST_H / 2), pacmanDir, ghostFeetFrame);
  }
}

// Draw smooth 0->100% fill over the calibrated visible area.
static void updateAndDrawFillBar() {
  const uint32_t now = millis();
  if (now - lastFillStepMs >= FILL_STEP_MS) {
    lastFillStepMs = now;
    if (fillWidth < ACTIVE_W) {
      fillWidth++;
    }
  }

  if (fillWidth > 0) {
    display.fillRect(ACTIVE_X, ACTIVE_Y, fillWidth, ACTIVE_H, SSD1306_WHITE);
  }

  // Keep the border visible while filling so it reads as a calibration bar.
  drawCalibrationOutline();

  if (fillWidth < ACTIVE_W) return;

  scrollX = ACTIVE_X + ACTIVE_W;
  scrollPasses = 0;
  setPhase(PHASE_SCROLL_TEXT);
}

// Render one complete frame for the active demo mode.
static void renderFrame() {
  display.clearDisplay();

  if (CALIBRATION_ONLY_MODE) {
    drawCalibrationOutline();
    display.display();
    return;
  }

  switch (phase) {
    case PHASE_SCROLL_TEXT:
      drawPhaseScrollText();
      break;
    case PHASE_SPACE_INVADERS:
      updateSpaceGame();
      drawSpaceGame();
      if (millis() - phaseStartMs >= SPACE_INVADERS_DURATION_MS) {
        setPhase(PHASE_PACMAN_RTL);
        initPacmanPass(PACMAN_DIR_LEFT);
      }
      break;
    case PHASE_PACMAN_RTL:
    case PHASE_PACMAN_LTR:
      updatePacmanScene();
      drawPacmanScene();
      break;
    case PHASE_CALIBRATION_HOLD:
      drawCalibrationOutline();
      if (millis() - phaseStartMs >= CALIBRATION_HOLD_MS) {
        fillWidth = 0;
        lastFillStepMs = millis();
        setPhase(PHASE_FILL_BAR);
      }
      break;
    case PHASE_FILL_BAR:
      updateAndDrawFillBar();
      break;
  }

  if (SHOW_CALIBRATION_GUIDE) {
    drawCalibrationOutline();
  }

  display.display();
}

// Arduino setup: initialize USB serial, I2C, OLED, and precompute text metrics.
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32C3_042_OLED_Animation");
  randomSeed((uint32_t)esp_random());

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);

  for (uint8_t addr : OLED_ADDRS) {
    if (oledPresentAt(addr)) {
      oledAddr = addr;
      break;
    }
  }

  if (oledAddr == 0) {
    Serial.println("OLED not found at 0x3C or 0x3D");
    scanI2c();
    while (true) {
      delay(1000);
    }
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, oledAddr)) {
    Serial.printf("OLED init failed at 0x%02X\n", oledAddr);
    scanI2c();
    while (true) {
      delay(1000);
    }
  }

  display.setTextWrap(false);
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(SCROLL_TEXT_SIZE);
  display.getTextBounds(SCROLL_TEXT, 0, 0, &x1, &y1, &w, &h);
  scrollTextWidth = (int16_t)w;
  phaseStartMs = millis();

  renderFrame();
  Serial.printf("OLED init OK at 0x%02X\n", oledAddr);
}

// Arduino loop: fixed-rate frame scheduler.
void loop() {
  const uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) return;
  lastFrameMs = now;
  frameCount++;
  renderFrame();
}
