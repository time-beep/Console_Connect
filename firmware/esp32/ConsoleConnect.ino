/*
  ============================================================
  MANAS CONSOLE — ESP32 DevKit Handheld Gaming Console
  SSD1306 OLED (128x64, I2C 0x3C) + Analog Joystick — ESP32 DevKit
  ============================================================

  CHANGELOG (this revision)
  - Added Breakout as a fully playable game. It gets its own
    4-option difficulty menu (Easy/Medium/Hard/Challenge) via a
    new BREAKOUT_DIFFICULTY_SELECT state — separate from the
    shared 3-option DIFFICULTY_SELECT used by Snake/Space
    Invaders/Pac-Man, so those are untouched.
  - Normal difficulties (Easy/Medium/Hard) loop endlessly on the
    same difficulty once bricks are cleared. Challenge mode
    auto-advances Easy -> Medium -> Hard with score/lives carried
    across stages, then shows a custom "game completed" screen and
    returns to the main menu. Handled as an internal overlay
    flag/timer, the same pattern already used for Pac-Man's
    "LEVEL COMPLETE" freeze.
  - Reuses existing helpers throughout: drawCell(), drawHeart(),
    formatScoreThreeDigits(), drawMenuList(), moveMenuCursor(),
    the shared Pause and Game Over screens (via `activeGame`).

  Everything else (Snake, Pong, Space Invaders, Pac-Man, main menu
  structure, difficulty framework for the other games, overall
  state machine) is unchanged from the previous revision.

  ARCHITECTURE (unchanged)
  - A single ConsoleState enum drives a top-level state machine.
  - Every screen has its own update*() and draw*() function pair.
  - Display code never touches game logic and vice versa.

  Libraries required (install via Arduino Library Manager):
  - Adafruit GFX Library
  - Adafruit SSD1306
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ============================================================
// USER CONFIGURATION — edit values here only
// ============================================================

// ---- Online profiles / Supabase ----
// Fill in your Wi-Fi credentials before uploading. Offline mode never connects.
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* SUPABASE_API_URL = "https://fuqcerwuyqwasozkxgqn.supabase.co/functions/v1/console-api";
// Public legacy anon key only. Never put the Supabase service-role key in firmware.
const char* SUPABASE_ANON_KEY = "YOUR_SUPABASE_ANON_OR_PUBLISHABLE_KEY";
const int MAX_ONLINE_USERS = 50;
const int ONLINE_GAME_COUNT = 6;
const char* ONLINE_GAME_KEYS[ONLINE_GAME_COUNT] = {"snake", "pong", "space_invaders", "pacman", "breakout", "tetris"};

// ---- ESP32 DevKit V1 / ESP32-WROOM-32 pin configuration ----
// OLED I2C: SDA GPIO21, SCL GPIO22
// Joystick: VRX GPIO34, VRY GPIO35, SW GPIO27
// IMPORTANT: these pins are for a classic ESP32 DevKit/WROOM-32, not ESP32-S3/C3.
#define OLED_SDA_PIN 21
#define OLED_SCL_PIN 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDRESS 0x3C

// ---- Joystick Pins ----
#define JOY_VRX_PIN 34  // ESP32 ADC1 input-only pin
#define JOY_VRY_PIN 35  // ESP32 ADC1 input-only pin
#define JOY_SW_PIN 27   // Digital input for joystick switch

// ---- Joystick Calibration (experimentally measured — kept exact) ----
const int JOYSTICK_CENTER_X = 510;
const int JOYSTICK_CENTER_Y = 523;
const int JOYSTICK_DEADZONE = 120;

// Orientation is rotated: X drives UP/DOWN, Y drives LEFT/RIGHT.
const int JOYSTICK_UP_THRESHOLD = 800;
const int JOYSTICK_DOWN_THRESHOLD = 220;
const int JOYSTICK_LEFT_THRESHOLD = 220;
const int JOYSTICK_RIGHT_THRESHOLD = 800;

const unsigned long BUTTON_DEBOUNCE_MS = 200;

// ---- Display Grid ----
const int CELL_SIZE = 4;
const int GRID_WIDTH = SCREEN_WIDTH / CELL_SIZE;    // 32 columns
const int GRID_HEIGHT = SCREEN_HEIGHT / CELL_SIZE;  // 16 rows

// ---- Snake Difficulty (movement interval, ms) ----
const unsigned long SNAKE_EASY_MS = 220;
const unsigned long SNAKE_MEDIUM_MS = 170;
const unsigned long SNAKE_HARD_MS = 120;
const int SNAKE_INITIAL_LENGTH = 3;
const int FOOD_EDGE_MARGIN = 2;

// ---- Pong Settings ----
const unsigned long PONG_SPEED_MS = 80;
const unsigned long PADDLE_MOVE_MS = 40;
const int PADDLE_HEIGHT = 4;
const int PLAYER_PADDLE_X = 1;
const int AI_PADDLE_X = GRID_WIDTH - 2;
const int PONG_WIN_SCORE = 5;
const unsigned long AI_MOVE_MS = 190;
const int AI_DEAD_ZONE = 1;

// ---- Space Invaders Difficulty (alien move interval / fire interval, ms) ----
const unsigned long SI_EASY_MOVE_MS = 500;
const unsigned long SI_EASY_FIRE_MS = 2600;
const unsigned long SI_MEDIUM_MOVE_MS = 350;
const unsigned long SI_MEDIUM_FIRE_MS = 1800;
const unsigned long SI_HARD_MOVE_MS = 220;
const unsigned long SI_HARD_FIRE_MS = 1200;

// ---- Space Invaders Settings ----
const int INVADER_ROWS = 2;
const int INVADER_COLS = 5;
const int INVADER_COUNT = INVADER_ROWS * INVADER_COLS;
const int INVADER_SPACING = 2;
const int SI_MAX_LIVES = 3;
const unsigned long SI_PLAYER_MOVE_MS = 60;
const unsigned long SI_BULLET_SPEED_MS = 60;
const unsigned long SI_PAUSE_HOLD_MS = 500;

// ---- Pac-Man Settings ----
const unsigned long PACMAN_MOVE_MS = 160;
const unsigned long PACMAN_GHOST_EASY_MS = 260;
const unsigned long PACMAN_GHOST_MEDIUM_MS = 190;
const unsigned long PACMAN_GHOST_HARD_MS = 130;
const int PACMAN_MAX_LIVES = 3;
const unsigned long PACMAN_LEVEL_COMPLETE_MS = 1500;
const unsigned long GHOST_BLINK_INTERVAL_MS = 300;
const int PACMAN_SPAWN_X = 16;
const int PACMAN_SPAWN_Y = 9;
const int GHOST_SPAWN_X[2] = { 13, 19 };
const int GHOST_SPAWN_Y[2] = { 9, 9 };

// ---- Breakout Difficulty (ball movement interval, ms) ----
const unsigned long BREAKOUT_EASY_MS = 140;
const unsigned long BREAKOUT_MEDIUM_MS = 100;
const unsigned long BREAKOUT_HARD_MS = 70;

// ---- Breakout Settings ----
const unsigned long BREAKOUT_PADDLE_MOVE_MS = 40;
const int BREAKOUT_PADDLE_WIDTH = 5;
const int BREAKOUT_PADDLE_Y = GRID_HEIGHT - 2;
const int BREAKOUT_BRICK_ROWS = 4;
const int BREAKOUT_BRICK_COLS = 6;
const int BREAKOUT_BRICK_COUNT = BREAKOUT_BRICK_ROWS * BREAKOUT_BRICK_COLS;
const int BREAKOUT_BRICK_SPACING_X = 5;  // 1 cell brick + 4 cell gap between columns
const int BREAKOUT_BRICK_SPACING_Y = 2;  // 1 cell brick + 1 cell gap between rows
const int BREAKOUT_MAX_LIVES = 3;
const unsigned long BREAKOUT_LEVEL_COMPLETE_MS = 1500;
const unsigned long BREAKOUT_FINAL_SCREEN_MS = 4000;

// ---- Menu / UI Settings ----
const int LINE_HEIGHT = 10;
const int MENU_VISIBLE_ITEMS = 5;
const unsigned long SPLASH_DURATION_MS = 4000;

// ============================================================
// END USER CONFIGURATION
// ============================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================
// STATE MACHINE
// ============================================================

enum ConsoleState {
  SPLASH,
  MODE_SELECT,
  USER_SELECT,
  USERNAME_SELECT,
  PASSWORD_ENTRY,
  CONFIRM_PASSWORD_ENTRY,
  PASSWORD_MISMATCH,
  USERNAME_TAKEN,
  WRONG_PASSWORD,
  NETWORK_ERROR,
  SYNC_ERROR,
  MAIN_MENU,
  HIGH_SCORE_INTRO,
  DIFFICULTY_SELECT,
  BREAKOUT_DIFFICULTY_SELECT,
  SNAKE,
  PONG,
  SPACE_INVADERS,
  PAC_MAN,
  BREAKOUT,
  TETRIS,
  ABOUT,
  PAUSED,
  GAME_OVER,
  UNDER_CONSTRUCTION
};

ConsoleState currentState;

ConsoleState activeGame = SNAKE;
ConsoleState pendingGameForDifficulty = SNAKE;

enum Difficulty {
  DIFF_EASY = 0,
  DIFF_MEDIUM = 1,
  DIFF_HARD = 2
};

Difficulty currentDifficulty = DIFF_MEDIUM;

enum JoystickDir {
  JOY_NONE,
  JOY_UP,
  JOY_DOWN,
  JOY_LEFT,
  JOY_RIGHT
};

enum Direction {
  DIR_UP,
  DIR_DOWN,
  DIR_LEFT,
  DIR_RIGHT
};

struct Point {
  int x;
  int y;
};

struct Invader {
  int x;
  int y;
  bool alive;
};

struct Brick {
  int x;
  int y;
  bool alive;
};

// ============================================================
// GLOBAL STATE — Input
// ============================================================

bool joystickWaitingForCenter = false;
bool lastButtonState = HIGH;
unsigned long lastButtonTime = 0;

// ============================================================
// GLOBAL STATE — Splash
// ============================================================

unsigned long splashStartTime = 0;

// Online-session state. Offline mode leaves all cloud score handling disabled.
bool onlineMode = false;
char selectedUserId[24] = "";
char directionPassword[5] = "";
char newUserPassword[5] = "";
int passwordLength = 0;
bool creatingNewUser = false;
char onlineUsers[MAX_ONLINE_USERS][24];
int onlineUserCount = 0;
int userSelectCursor = 0;
int userSelectScrollOffset = 0;
int usernameSelectCursor = 0;
int usernameSelectScrollOffset = 0;
int availableUsernameIndices[50];
int availableUsernameCount = 0;
int modeCursor = 0;
ConsoleState scoreIntroGame = SNAKE;
unsigned long messageScreenStarted = 0;
const unsigned long MESSAGE_SCREEN_MS = 1400;
const char* CREW_NAMES[] = {
  "RedToaster", "EggRice", "MoonSpoon", "ToastWizard", "PickleRocket",
  "NoodleGoblin", "CloudPotato", "TurboMango", "WaffleNinja", "CheeseComet",
  "SockDragon", "PastaWizard", "DiscoTurnip", "PixelPickle", "BananaHelmet",
  "CactusPancake", "JellyBoots", "CosmicToast", "MangoBucket", "FuzzyLadder",
  "SoupPirate", "MarbleDuck", "GrapeEngine", "SleepyFork", "RobotMuffin",
  "TinyVolcano", "PillowKnight", "SpicyTeacup", "LaserCarrot", "DoodleTurtle",
  "CrankyDonut", "BubbleHammer", "KiwiRocket", "VelvetPotato", "WobbleCheese",
  "FriedPlanet", "SodaGoblin", "CrispyMoon", "BiscuitNinja", "RubberWizard",
  "PickleSocks", "CosmicBucket", "PuddingPilot", "SneakyWaffle", "MossyRobot",
  "TacoLantern", "GummyCompass", "RogueMuffin", "MarshmallowJet", "OnionSatellite"
};
const int CREW_NAME_COUNT = sizeof(CREW_NAMES) / sizeof(CREW_NAMES[0]);
int cloudHighScores[ONLINE_GAME_COUNT] = {0, 0, 0, 0, 0, 0};
int pendingScores[ONLINE_GAME_COUNT] = {-1, -1, -1, -1, -1, -1};

// ============================================================
// GLOBAL STATE — Main Menu
// ============================================================

const char* mainMenuItems[] = { "Snake", "Pong", "Space Invaders", "Pac-Man", "Breakout", "Tetris", "About", "Logout" };
const int OFFLINE_MENU_ITEM_COUNT = 7;
const int ONLINE_MENU_ITEM_COUNT = 8;
int mainMenuCursor = 0;
int mainMenuScrollOffset = 0;

// ============================================================
// GLOBAL STATE — Difficulty Select (Snake / Space Invaders / Pac-Man)
// ============================================================

const char* difficultyItems[] = { "Easy", "Medium", "Hard" };
const int DIFFICULTY_ITEM_COUNT = 3;
int difficultyCursor = (int)DIFF_MEDIUM;

// ============================================================
// GLOBAL STATE — Difficulty Select (Breakout only — 4 options)
// ============================================================

const char* breakoutDifficultyItems[] = { "Easy", "Medium", "Hard", "Challenge" };
const int BREAKOUT_DIFFICULTY_ITEM_COUNT = 4;
int breakoutDifficultyCursor = 0;

// ============================================================
// GLOBAL STATE — Pause Menu
// ============================================================

const char* pauseMenuItems[] = { "Continue", "Restart", "Main Menu" };
const int PAUSE_MENU_ITEM_COUNT = 3;
int pauseMenuCursor = 0;
unsigned long pausedAt = 0;

// ============================================================
// GLOBAL STATE — Game Over Menu
// ============================================================

const char* gameOverItems[] = { "Restart", "Main Menu" };
const int GAME_OVER_ITEM_COUNT = 2;
int gameOverCursor = 0;

// ============================================================
// GLOBAL STATE — Snake Game
// ============================================================

const int MAX_SNAKE_LENGTH = GRID_WIDTH * GRID_HEIGHT;
Point snakeBody[MAX_SNAKE_LENGTH];
int snakeLength;
Direction snakeCurrentDirection;
Direction snakePendingDirection;
Point snakeFood;
int snakeScore;
unsigned long snakeLastMoveTime = 0;
unsigned long snakeMoveIntervalMs = SNAKE_MEDIUM_MS;

// ============================================================
// GLOBAL STATE — Pong Game
// ============================================================

int playerPaddleY;
int aiPaddleY;

int ballX, ballY;
int ballDX, ballDY;

int playerScore;
int aiScore;

unsigned long pongLastMoveTime = 0;
unsigned long playerPaddleLastMoveTime = 0;
unsigned long aiLastMoveTime = 0;

// ============================================================
// GLOBAL STATE — Space Invaders
// ============================================================

Invader invaders[INVADER_COUNT];
int siDirection = 1;

int siPlayerX;
int siPlayerY;

Point siPlayerBullet;
bool siPlayerBulletActive = false;

Point siAlienBullet;
bool siAlienBulletActive = false;

int siScore;
int siLives;

unsigned long siMoveIntervalMs = SI_MEDIUM_MOVE_MS;
unsigned long siFireIntervalMs = SI_MEDIUM_FIRE_MS;

unsigned long siPlayerLastMoveTime = 0;
unsigned long siAlienLastMoveTime = 0;
unsigned long siAlienFireLastTime = 0;
unsigned long siPlayerBulletLastMoveTime = 0;
unsigned long siAlienBulletLastMoveTime = 0;

bool siButtonWasHeld = false;
unsigned long siButtonDownTime = 0;
bool siLongPressHandled = false;

// ============================================================
// GLOBAL STATE — Pac-Man
// ============================================================

bool pelletPresent[GRID_HEIGHT][GRID_WIDTH];
int pacmanPelletsRemaining;

int pacmanX, pacmanY;
Direction pacmanCurrentDir;
Direction pacmanDesiredDir;

int ghostX[2], ghostY[2];
Direction ghostDir[2];

int pacmanScore;
int pacmanLives;

unsigned long pacmanLastMoveTime = 0;
unsigned long ghostLastMoveTime[2] = { 0, 0 };
unsigned long pacmanGhostMoveIntervalMs = PACMAN_GHOST_MEDIUM_MS;

bool pacmanLevelCompleteActive = false;
unsigned long pacmanLevelCompleteStart = 0;

// ============================================================
// GLOBAL STATE — Breakout
// ============================================================

enum BreakoutMode {
  BREAKOUT_MODE_NORMAL,
  BREAKOUT_MODE_CHALLENGE
};

BreakoutMode breakoutMode = BREAKOUT_MODE_NORMAL;
Difficulty breakoutDifficulty = DIFF_MEDIUM;  // also doubles as "current stage" in Challenge mode

Brick bricks[BREAKOUT_BRICK_COUNT];

int breakoutPaddleX;
int breakoutBallX, breakoutBallY;
int breakoutBallDX, breakoutBallDY;

int breakoutScore;
int breakoutLives;

unsigned long breakoutMoveIntervalMs = BREAKOUT_MEDIUM_MS;
unsigned long breakoutLastMoveTime = 0;
unsigned long breakoutPaddleLastMoveTime = 0;

// What should happen once the current "LEVEL COMPLETE"-style overlay
// finishes — mirrors Pac-Man's level-complete freeze, but Breakout has
// several distinct follow-up actions instead of just one.
enum BreakoutOverlayAction {
  BO_NONE,
  BO_RESET_SAME,
  BO_ADVANCE_MEDIUM,
  BO_ADVANCE_HARD,
  BO_RETURN_MENU
};

bool breakoutOverlayActive = false;
unsigned long breakoutOverlayStart = 0;
unsigned long breakoutOverlayDurationMs = 0;
BreakoutOverlayAction breakoutOverlayAction = BO_NONE;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(JOY_SW_PIN, INPUT_PULLUP);

  // ESP32 Arduino I2C pin assignment
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  // Keep the original Pico joystick thresholds in the same 0-1023 range.
  // This avoids needing to rescale the existing joystick calibration.
  analogReadResolution(10);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;) delay(10);
  }

  display.clearDisplay();
  display.display();

  randomSeed(analogRead(JOY_VRX_PIN));

  currentState = SPLASH;
  splashStartTime = millis();
}

// ============================================================
// LOOP — top-level state machine dispatch
// ============================================================
void loop() {
  ConsoleState stateAtLoopStart = currentState;
  JoystickDir joyDir;
  JoystickDir heldDir;
  bool buttonPressed;
  bool buttonHeld;
  readJoystick(joyDir, heldDir, buttonPressed, buttonHeld);

  switch (currentState) {
    case SPLASH:
      updateSplash();
      drawSplash();
      break;

    case MODE_SELECT:
      updateModeSelect(joyDir, buttonPressed);
      drawModeSelect();
      break;

    case USER_SELECT:
      updateUserSelect(joyDir, buttonPressed);
      drawUserSelect();
      break;

    case USERNAME_SELECT:
      updateUsernameSelect(joyDir, buttonPressed);
      drawUsernameSelect();
      break;

    case PASSWORD_ENTRY:
      updatePasswordEntry(joyDir, buttonPressed);
      drawPasswordEntry();
      break;

    case CONFIRM_PASSWORD_ENTRY:
      updateConfirmPasswordEntry(joyDir, buttonPressed);
      drawConfirmPasswordEntry();
      break;

    case WRONG_PASSWORD:
      drawSimpleMessage("WRONG PASSWORD", "Try again");
      if (millis() - messageScreenStarted >= MESSAGE_SCREEN_MS) { passwordLength = 0; directionPassword[0] = '\0'; currentState = PASSWORD_ENTRY; }
      break;

    case PASSWORD_MISMATCH:
      drawSimpleMessage("PASSWORDS DIFFER", "Start again");
      if (millis() - messageScreenStarted >= MESSAGE_SCREEN_MS) {
        passwordLength = 0; directionPassword[0] = '\0'; newUserPassword[0] = '\0';
        currentState = PASSWORD_ENTRY;
      }
      break;

    case USERNAME_TAKEN:
      drawSimpleMessage("NAME TAKEN!", "Choose another");
      if (millis() - messageScreenStarted >= MESSAGE_SCREEN_MS) {
        rebuildAvailableUsernames(); currentState = USERNAME_SELECT;
      }
      break;

    case NETWORK_ERROR:
      drawSimpleMessage("NETWORK ERROR", "Press to retry");
      if (buttonPressed) {
        currentState = MODE_SELECT;
        modeCursor = 0;
      }
      break;

    case SYNC_ERROR:
      drawSimpleMessage("SYNC FAILED", "Press to retry");
      if (buttonPressed) {
        if (syncPendingScores()) finishLogout();
        else messageScreenStarted = millis();
      }
      break;

    case MAIN_MENU:
      updateMainMenu(joyDir, buttonPressed);
      drawMainMenu();
      break;

    case HIGH_SCORE_INTRO:
      if (buttonPressed) startGameAfterScoreIntro();
      drawHighScoreIntro();
      break;

    case DIFFICULTY_SELECT:
      updateDifficultySelect(joyDir, buttonPressed);
      drawDifficultySelect();
      break;

    case BREAKOUT_DIFFICULTY_SELECT:
      updateBreakoutDifficultySelect(joyDir, buttonPressed);
      drawBreakoutDifficultySelect();
      break;

    case SNAKE:
      updateSnakeState(joyDir, buttonPressed);
      drawSnakeScreen();
      break;

    case PONG:
      updatePong(joyDir, heldDir, buttonPressed);
      drawPong();
      break;

    case SPACE_INVADERS:
      updateSpaceInvaders(heldDir, buttonHeld);
      drawSpaceInvaders();
      break;

    case PAC_MAN:
      updatePacmanState(heldDir, buttonPressed);
      drawPacmanScreen();
      break;

    case BREAKOUT:
      updateBreakoutState(heldDir, buttonPressed);
      drawBreakoutScreen();
      break;

    case PAUSED:
      updatePauseMenu(joyDir, buttonPressed);
      drawPauseMenu();
      break;

    case GAME_OVER:
      updateGameOverScreen(joyDir, buttonPressed);
      drawGameOverScreen();
      break;

    case ABOUT:
      updateAboutScreen(buttonPressed);
      drawAboutScreen();
      break;

    case UNDER_CONSTRUCTION:
      updateUnderConstruction(buttonPressed);
      drawUnderConstruction();
      break;

    case TETRIS:
      // Not implemented yet. Main menu routes this to UNDER_CONSTRUCTION
      // instead, so this state is never actually entered — kept here
      // only so the switch stays exhaustive once Tetris gets built.
      currentState = MAIN_MENU;
      break;
  }

  // Detect a game-over transition after game logic runs, so each final score
  // is processed exactly once without changing any individual game's logic.
  bool wasPlaying = (stateAtLoopStart == SNAKE || stateAtLoopStart == PONG ||
                     stateAtLoopStart == SPACE_INVADERS || stateAtLoopStart == PAC_MAN ||
                     stateAtLoopStart == BREAKOUT);
  // Save the final score both when gameplay ends naturally and when the
  // player abandons a game via PAUSED -> Main Menu. PAUSED is deliberately
  // handled here (rather than inside updatePauseMenu) so the shared score
  // capture path runs once after the menu selection has changed state.
  bool abandonedToMainMenu = (stateAtLoopStart == PAUSED && currentState == MAIN_MENU);
  if ((wasPlaying && (currentState == GAME_OVER || currentState == MAIN_MENU)) ||
      abandonedToMainMenu) {
    captureGameOverScore();
  }
}

// ============================================================
// INPUT
// ============================================================

void readJoystick(JoystickDir &dir, JoystickDir &heldDir, bool &buttonPressed, bool &buttonHeld) {
  dir = JOY_NONE;
  heldDir = JOY_NONE;
  buttonPressed = false;

  int rawX = analogRead(JOY_VRX_PIN);
  int rawY = analogRead(JOY_VRY_PIN);

  JoystickDir detected = JOY_NONE;
  if (rawX > JOYSTICK_UP_THRESHOLD) {
    detected = JOY_UP;
  } else if (rawX < JOYSTICK_DOWN_THRESHOLD) {
    detected = JOY_DOWN;
  } else if (rawY < JOYSTICK_LEFT_THRESHOLD) {
    detected = JOY_LEFT;
  } else if (rawY > JOYSTICK_RIGHT_THRESHOLD) {
    detected = JOY_RIGHT;
  }

  heldDir = detected;

  bool isCentered = (abs(rawX - JOYSTICK_CENTER_X) < JOYSTICK_DEADZONE) &&
                     (abs(rawY - JOYSTICK_CENTER_Y) < JOYSTICK_DEADZONE);

  if (isCentered) {
    joystickWaitingForCenter = false;
  }

  if (!joystickWaitingForCenter && detected != JOY_NONE) {
    dir = detected;
    joystickWaitingForCenter = true;
  }

  bool currentButtonState = digitalRead(JOY_SW_PIN);
  buttonHeld = (currentButtonState == LOW);

  if (currentButtonState == LOW && lastButtonState == HIGH &&
      (millis() - lastButtonTime) >= BUTTON_DEBOUNCE_MS) {
    buttonPressed = true;
    lastButtonTime = millis();
  }
  lastButtonState = currentButtonState;
}

// ============================================================
// GENERIC HELPERS
// ============================================================

void drawCell(int gridX, int gridY) {
  display.fillRect(gridX * CELL_SIZE, gridY * CELL_SIZE, CELL_SIZE, CELL_SIZE, SSD1306_WHITE);
}

void drawHollowSquare(int gridX, int gridY) {
  drawCell(gridX, gridY);
  int px = gridX * CELL_SIZE;
  int py = gridY * CELL_SIZE;
  display.fillRect(px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2, SSD1306_BLACK);
}

void formatScoreThreeDigits(int score, char* buffer, size_t bufferSize) {
  int clamped = score;
  if (clamped < 0) clamped = 0;
  if (clamped > 999) clamped = 999;
  snprintf(buffer, bufferSize, "%03d", clamped);
}

void moveMenuCursor(int &cursor, int &scrollOffset, int delta, int itemCount, int visibleItems) {
  cursor += delta;
  if (cursor < 0) cursor = 0;
  if (cursor > itemCount - 1) cursor = itemCount - 1;

  if (cursor < scrollOffset) {
    scrollOffset = cursor;
  }
  if (cursor >= scrollOffset + visibleItems) {
    scrollOffset = cursor - visibleItems + 1;
  }
}

void drawMenuList(const char* title, const char* const items[], int itemCount,
                   int cursor, int scrollOffset, int visibleItems) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int y = 0;
  if (title != nullptr) {
    display.setCursor(0, y);
    display.print(title);
    y += LINE_HEIGHT;
  }

  int lastVisible = min(scrollOffset + visibleItems, itemCount);
  for (int i = scrollOffset; i < lastVisible; i++) {
    display.setCursor(0, y);
    display.print(i == cursor ? "> " : "  ");
    display.print(items[i]);
    y += LINE_HEIGHT;
  }

  display.display();
}

void drawHeart(int x, int y, bool filled) {
  if (filled) {
    display.fillCircle(x + 2, y + 2, 2, SSD1306_WHITE);
    display.fillCircle(x + 5, y + 2, 2, SSD1306_WHITE);
    display.fillTriangle(x, y + 3, x + 7, y + 3, x + 3, y + 7, SSD1306_WHITE);
  } else {
    display.drawCircle(x + 2, y + 2, 2, SSD1306_WHITE);
    display.drawCircle(x + 5, y + 2, 2, SSD1306_WHITE);
    display.drawTriangle(x, y + 3, x + 7, y + 3, x + 3, y + 7, SSD1306_WHITE);
  }
}

// ============================================================
// SPLASH SCREEN
// ============================================================

void updateSplash() {
  if (millis() - splashStartTime >= SPLASH_DURATION_MS) {
    modeCursor = 0;
    currentState = MODE_SELECT;
  }
}

void drawSplash() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(20, 16);
  display.print(F("PS-67"));

  display.setCursor(40, 32);
  display.print(F("Version 1.0"));

  display.setCursor(16, 48);
  display.print(F("Powered by ESP32"));

  display.display();
}

// ============================================================
// ONLINE / OFFLINE MODE AND PROFILE LOGIN
// ============================================================

bool apiPost(const String& payload, String& response) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // Prototype: TLS encrypted, but server certificate isn't verified.
  HTTPClient https;
  https.setTimeout(9000);
  if (!https.begin(secureClient, SUPABASE_API_URL)) return false;
  https.addHeader("Content-Type", "application/json");
  https.addHeader("apikey", SUPABASE_ANON_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  int code = https.POST(payload);
  response = code > 0 ? https.getString() : String("");
  https.end();
  return code >= 200 && code < 300;
}

bool connectAndLoadUsers() {
  if (String(WIFI_SSID) == "YOUR_WIFI_NAME" || String(WIFI_PASSWORD) == "YOUR_WIFI_PASSWORD") return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(100);
  if (WiFi.status() != WL_CONNECTED) return false;

  String response;
  if (!apiPost("{\"action\":\"list_users\"}", response)) return false;
  int usersKey = response.indexOf("\"users\":[");
  if (usersKey < 0) return false;
  onlineUserCount = 0;
  int pos = response.indexOf('[', usersKey) + 1;
  while (pos > 0 && pos < (int)response.length() && onlineUserCount < MAX_ONLINE_USERS) {
    while (pos < (int)response.length() && (response[pos] == ' ' || response[pos] == ',')) pos++;
    if (pos >= (int)response.length() || response[pos] == ']') break;
    if (response[pos] != '"') break;
    int end = response.indexOf('"', pos + 1);
    if (end < 0) break;
    String id = response.substring(pos + 1, end);
    id.toCharArray(onlineUsers[onlineUserCount], sizeof(onlineUsers[0]));
    onlineUserCount++;
    pos = end + 1;
  }
  userSelectCursor = 0;
  userSelectScrollOffset = 0;
  return onlineUserCount > 0;
}

void updateModeSelect(JoystickDir joyDir, bool buttonPressed) {
  if (joyDir == JOY_UP || joyDir == JOY_DOWN) modeCursor = 1 - modeCursor;
  if (!buttonPressed) return;
  if (modeCursor == 1) {
    // Offline mode deliberately avoids Wi-Fi, login, and persistent scores.
    onlineMode = false;
    selectedUserId[0] = '\0';
    for (int i = 0; i < ONLINE_GAME_COUNT; i++) { cloudHighScores[i] = 0; pendingScores[i] = -1; }
    mainMenuCursor = 0; mainMenuScrollOffset = 0; currentState = MAIN_MENU;
  } else {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 20); display.print(F("CONNECTING WIFI"));
    display.setCursor(20, 36); display.print(F("PLEASE WAIT")); display.display();
    if (connectAndLoadUsers()) currentState = USER_SELECT;
    else { currentState = NETWORK_ERROR; messageScreenStarted = millis(); }
  }
}

void drawModeSelect() {
  const char* items[] = {"Online", "Offline"};
  drawMenuList("SELECT MODE", items, 2, modeCursor, 0, 2);
}

void updateUserSelect(JoystickDir joyDir, bool buttonPressed) {
  int itemCount = onlineUserCount + 1; // Existing accounts plus the New User option.
  if (joyDir == JOY_UP) moveMenuCursor(userSelectCursor, userSelectScrollOffset, -1, itemCount, MENU_VISIBLE_ITEMS);
  else if (joyDir == JOY_DOWN) moveMenuCursor(userSelectCursor, userSelectScrollOffset, 1, itemCount, MENU_VISIBLE_ITEMS);
  if (!buttonPressed) return;

  if (userSelectCursor == onlineUserCount) {
    rebuildAvailableUsernames();
    usernameSelectCursor = 0;
    usernameSelectScrollOffset = 0;
    currentState = USERNAME_SELECT;
    return;
  }

  strncpy(selectedUserId, onlineUsers[userSelectCursor], sizeof(selectedUserId) - 1);
  selectedUserId[sizeof(selectedUserId) - 1] = '\0';
  creatingNewUser = false;
  passwordLength = 0; directionPassword[0] = '\0';
  currentState = PASSWORD_ENTRY;
}

void drawUserSelect() {
  const char* items[MAX_ONLINE_USERS + 1];
  for (int i = 0; i < onlineUserCount; i++) items[i] = onlineUsers[i];
  items[onlineUserCount] = "+ New User";
  drawMenuList("SELECT USER", items, onlineUserCount + 1, userSelectCursor, userSelectScrollOffset, MENU_VISIBLE_ITEMS);
}

bool usernameExistsLocally(const char* name) {
  for (int i = 0; i < onlineUserCount; i++) {
    if (strcmp(onlineUsers[i], name) == 0) return true;
  }
  return false;
}

void rebuildAvailableUsernames() {
  availableUsernameCount = 0;
  for (int i = 0; i < CREW_NAME_COUNT; i++) {
    if (!usernameExistsLocally(CREW_NAMES[i])) availableUsernameIndices[availableUsernameCount++] = i;
  }
  if (usernameSelectCursor >= availableUsernameCount) usernameSelectCursor = max(0, availableUsernameCount - 1);
  usernameSelectScrollOffset = min(usernameSelectScrollOffset, max(0, availableUsernameCount - MENU_VISIBLE_ITEMS));
}

void updateUsernameSelect(JoystickDir joyDir, bool buttonPressed) {
  if (availableUsernameCount <= 0) return;
  // Carousel: wrap around through the available crew names.
  if (joyDir == JOY_UP || joyDir == JOY_RIGHT) usernameSelectCursor = (usernameSelectCursor + 1) % availableUsernameCount;
  else if (joyDir == JOY_DOWN || joyDir == JOY_LEFT) usernameSelectCursor = (usernameSelectCursor - 1 + availableUsernameCount) % availableUsernameCount;
  if (buttonPressed) {
    int nameIndex = availableUsernameIndices[usernameSelectCursor];
    strncpy(selectedUserId, CREW_NAMES[nameIndex], sizeof(selectedUserId) - 1);
    selectedUserId[sizeof(selectedUserId) - 1] = '\0';
    creatingNewUser = true;
    newUserPassword[0] = '\0';
    directionPassword[0] = '\0';
    passwordLength = 0;
    currentState = PASSWORD_ENTRY;
  }
}

void drawUsernameSelect() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 0);
  display.print(F("CHOOSE CREW NAME"));
  if (availableUsernameCount <= 0) {
    display.setCursor(12, 28); display.print(F("No names available"));
    display.display(); return;
  }
  int previous = (usernameSelectCursor - 1 + availableUsernameCount) % availableUsernameCount;
  int next = (usernameSelectCursor + 1) % availableUsernameCount;
  display.setCursor(0, 15); display.print(F("< ")); display.print(CREW_NAMES[availableUsernameIndices[previous]]);
  display.setCursor(0, 51); display.print(F("> ")); display.print(CREW_NAMES[availableUsernameIndices[next]]);
  const char* current = CREW_NAMES[availableUsernameIndices[usernameSelectCursor]];
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(2);
  display.getTextBounds(current, 0, 0, &x1, &y1, &w, &h);
  if (w > 124) { display.setTextSize(1); display.getTextBounds(current, 0, 0, &x1, &y1, &w, &h); }
  display.setCursor((SCREEN_WIDTH - w) / 2, 30);
  display.print(current);
  display.display();
}

void recordPasswordDirection(JoystickDir joyDir) {
  if (joyDir != JOY_NONE && passwordLength < 4) {
    char c = 'U';
    if (joyDir == JOY_DOWN) c = 'D'; else if (joyDir == JOY_LEFT) c = 'L'; else if (joyDir == JOY_RIGHT) c = 'R';
    directionPassword[passwordLength++] = c;
    directionPassword[passwordLength] = '\0';
  }
}

void updatePasswordEntry(JoystickDir joyDir, bool buttonPressed) {
  recordPasswordDirection(joyDir);
  if (!buttonPressed || passwordLength != 4) return;

  if (creatingNewUser) {
    strncpy(newUserPassword, directionPassword, sizeof(newUserPassword));
    passwordLength = 0; directionPassword[0] = '\0';
    currentState = CONFIRM_PASSWORD_ENTRY;
    return;
  }

  String payload = String("{\"action\":\"login\",\"user_id\":\"") + selectedUserId +
    "\",\"password\":\"" + directionPassword + "\"}";
  String response;
  if (apiPost(payload, response) && response.indexOf("\"ok\":true") >= 0) {
    for (int i = 0; i < ONLINE_GAME_COUNT; i++) cloudHighScores[i] = jsonScore(response, ONLINE_GAME_KEYS[i]);
    for (int i = 0; i < ONLINE_GAME_COUNT; i++) pendingScores[i] = -1;
    onlineMode = true; mainMenuCursor = 0; mainMenuScrollOffset = 0; currentState = MAIN_MENU;
  } else if (response.indexOf("Wrong user ID or password") >= 0) {
    passwordLength = 0; directionPassword[0] = '\0'; currentState = WRONG_PASSWORD; messageScreenStarted = millis();
  } else {
    currentState = NETWORK_ERROR; messageScreenStarted = millis();
  }
}

void updateConfirmPasswordEntry(JoystickDir joyDir, bool buttonPressed) {
  recordPasswordDirection(joyDir);
  if (!buttonPressed || passwordLength != 4) return;
  if (strcmp(newUserPassword, directionPassword) != 0) {
    passwordLength = 0; directionPassword[0] = '\0'; newUserPassword[0] = '\0';
    currentState = PASSWORD_MISMATCH; messageScreenStarted = millis();
    return;
  }

  String payload = String("{\"action\":\"create_user\",\"user_id\":\"") + selectedUserId +
    "\",\"password\":\"" + newUserPassword + "\"}";
  String response;
  if (apiPost(payload, response) && response.indexOf("\"ok\":true") >= 0) {
    for (int i = 0; i < ONLINE_GAME_COUNT; i++) cloudHighScores[i] = 0;
    for (int i = 0; i < ONLINE_GAME_COUNT; i++) pendingScores[i] = -1;
    onlineMode = true;
    if (onlineUserCount < MAX_ONLINE_USERS) {
      strncpy(onlineUsers[onlineUserCount], selectedUserId, sizeof(onlineUsers[0]) - 1);
      onlineUsers[onlineUserCount][sizeof(onlineUsers[0]) - 1] = '\0';
      onlineUserCount++;
    }
    passwordLength = 0; directionPassword[0] = '\0'; newUserPassword[0] = '\0';
    mainMenuCursor = 0; mainMenuScrollOffset = 0; currentState = MAIN_MENU;
  } else if (response.indexOf("already exists") >= 0 || response.indexOf("Name already taken") >= 0) {
    passwordLength = 0; directionPassword[0] = '\0'; newUserPassword[0] = '\0';
    currentState = USERNAME_TAKEN; messageScreenStarted = millis();
  } else {
    currentState = NETWORK_ERROR; messageScreenStarted = millis();
  }
}

void drawPasswordEntry() {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.print(creatingNewUser ? F("NEW USER: ") : F("USER: ")); display.print(selectedUserId);
  display.setCursor(0, 16); display.print(F("ENTER 4 DIRECTIONS"));
  for (int i = 0; i < 4; i++) {
    int cx = 12 + i * 30;
    int cy = 39;
    if (i < passwordLength) drawDirectionArrow(directionPassword[i], cx, cy);
    else { display.setCursor(cx - 3, cy - 4); display.print('_'); }
  }
  display.setCursor(0, 52); display.print(F("SW=SUBMIT (AFTER 4)"));
  display.display();
}

void drawConfirmPasswordEntry() {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.print(F("CONFIRM PASSWORD"));
  display.setCursor(0, 12); display.print(F("USER: ")); display.print(selectedUserId);
  display.setCursor(0, 25); display.print(F("RE-ENTER DIRECTIONS"));
  for (int i = 0; i < 4; i++) {
    int cx = 12 + i * 30;
    int cy = 43;
    if (i < passwordLength) drawDirectionArrow(directionPassword[i], cx, cy);
    else { display.setCursor(cx - 3, cy - 4); display.print('_'); }
  }
  display.setCursor(0, 56); display.print(F("SW=CONFIRM"));
  display.display();
}

void drawDirectionArrow(char dir, int cx, int cy) {
  if (dir == 'U') {
    display.drawLine(cx, cy + 5, cx, cy - 2, SSD1306_WHITE);
    display.fillTriangle(cx, cy - 5, cx - 3, cy - 1, cx + 3, cy - 1, SSD1306_WHITE);
  } else if (dir == 'D') {
    display.drawLine(cx, cy - 5, cx, cy + 2, SSD1306_WHITE);
    display.fillTriangle(cx, cy + 5, cx - 3, cy + 1, cx + 3, cy + 1, SSD1306_WHITE);
  } else if (dir == 'L') {
    display.drawLine(cx + 5, cy, cx - 2, cy, SSD1306_WHITE);
    display.fillTriangle(cx - 5, cy, cx - 1, cy - 3, cx - 1, cy + 3, SSD1306_WHITE);
  } else {
    display.drawLine(cx - 5, cy, cx + 2, cy, SSD1306_WHITE);
    display.fillTriangle(cx + 5, cy, cx + 1, cy - 3, cx + 1, cy + 3, SSD1306_WHITE);
  }
}

void drawSimpleMessage(const char* line1, const char* line2) {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 18); display.print(line1);
  display.setCursor(8, 38); display.print(line2);
  display.display();
}

int jsonScore(const String& json, const char* key) {
  String needle = String("\"") + key + "\":";
  int p = json.indexOf(needle);
  if (p < 0) return 0;
  p += needle.length();
  while (p < (int)json.length() && json[p] == ' ') p++;
  int end = p;
  while (end < (int)json.length() && json[end] >= '0' && json[end] <= '9') end++;
  return json.substring(p, end).toInt();
}

int scoreForGame(ConsoleState game) {
  if (game == SNAKE) return snakeScore;
  if (game == PONG) return playerScore;
  if (game == SPACE_INVADERS) return siScore;
  if (game == PAC_MAN) return pacmanScore;
  if (game == BREAKOUT) return breakoutScore;
  return 0; // Tetris is not implemented yet.
}

int gameIndexForState(ConsoleState game) {
  if (game == SNAKE) return 0;
  if (game == PONG) return 1;
  if (game == SPACE_INVADERS) return 2;
  if (game == PAC_MAN) return 3;
  if (game == BREAKOUT) return 4;
  if (game == TETRIS) return 5;
  return -1;
}

bool sendScoreUpdate(int index, int score) {
  if (index < 0 || index >= ONLINE_GAME_COUNT || !onlineMode) return true;
  String payload = String("{\"action\":\"update_score\",\"user_id\":\"") + selectedUserId +
    "\",\"password\":\"" + directionPassword + "\",\"game\":\"" + ONLINE_GAME_KEYS[index] +
    "\",\"score\":" + String(score) + "}";
  String response;
  if (!apiPost(payload, response) || response.indexOf("\"ok\":true") < 0) return false;
  cloudHighScores[index] = max(cloudHighScores[index], jsonScore(response, "high_score"));
  pendingScores[index] = -1;
  return true;
}

void captureGameOverScore() {
  if (!onlineMode) return;
  int index = gameIndexForState(activeGame);
  if (index < 0) return;
  int score = scoreForGame(activeGame);
  if (score > cloudHighScores[index]) {
    cloudHighScores[index] = score;
    pendingScores[index] = score;
    sendScoreUpdate(index, score);
  }
}

bool syncPendingScores() {
  if (!onlineMode) return true;
  bool allOk = true;
  for (int i = 0; i < ONLINE_GAME_COUNT; i++) {
    if (pendingScores[i] >= 0 && !sendScoreUpdate(i, pendingScores[i])) allOk = false;
  }
  return allOk;
}

void finishLogout() {
  // Log out of the current account, but keep Wi-Fi connected for quick
  // account switching. Return directly to the online user carousel.
  onlineMode = true;
  selectedUserId[0] = '\0';
  directionPassword[0] = '\0';
  newUserPassword[0] = '\0';
  passwordLength = 0;
  creatingNewUser = false;

  // Refresh the available online accounts while reusing the current Wi-Fi
  // connection. If the refresh fails, retain the existing user list.
  connectAndLoadUsers();

  userSelectCursor = 0;
  userSelectScrollOffset = 0;
  for (int i = 0; i < ONLINE_GAME_COUNT; i++) {
    cloudHighScores[i] = 0;
    pendingScores[i] = -1;
  }
  mainMenuCursor = 0;
  mainMenuScrollOffset = 0;
  currentState = USER_SELECT;
}

// ============================================================
// PRE-GAME HIGH SCORE INTRO
// ============================================================
const char* gameNameForState(ConsoleState game) {
  switch (game) {
    case SNAKE: return "SNAKE";
    case PONG: return "PONG";
    case SPACE_INVADERS: return "SPACE INVADERS";
    case PAC_MAN: return "PAC-MAN";
    case BREAKOUT: return "BREAKOUT";
    case TETRIS: return "TETRIS";
    default: return "GAME";
  }
}
void showHighScoreIntro(ConsoleState game) {
  scoreIntroGame = game;
  currentState = HIGH_SCORE_INTRO;
}
void drawHighScoreIntro() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  const char* name = gameNameForState(scoreIntroGame);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 2); display.print(name);
  int index = gameIndexForState(scoreIntroGame);
  int score = (onlineMode && index >= 0) ? cloudHighScores[index] : 0;
  display.setCursor(18, 19); display.print(F("YOUR HIGH SCORE"));
  char scoreText[12]; snprintf(scoreText, sizeof(scoreText), "%d", score);
  display.setTextSize(2); display.getTextBounds(scoreText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 31); display.print(scoreText);
  display.setTextSize(1); display.setCursor(16, 55); display.print(F("PRESS TO PLAY"));
  display.display();
}
void startGameAfterScoreIntro() {
  ConsoleState game = scoreIntroGame;
  if (game == SNAKE) { startSnake(currentDifficulty); currentState = SNAKE; }
  else if (game == PONG) { startPong(); currentState = PONG; }
  else if (game == SPACE_INVADERS) { startSpaceInvaders(currentDifficulty); currentState = SPACE_INVADERS; }
  else if (game == PAC_MAN) { startPacman(currentDifficulty); currentState = PAC_MAN; }
  else if (game == BREAKOUT) {
    if (breakoutDifficultyCursor == 3) startBreakout(DIFF_EASY, BREAKOUT_MODE_CHALLENGE);
    else startBreakout((Difficulty)breakoutDifficultyCursor, BREAKOUT_MODE_NORMAL);
    currentState = BREAKOUT;
  } else currentState = MAIN_MENU;
}

// ============================================================
// MAIN MENU
// ============================================================

void updateMainMenu(JoystickDir joyDir, bool buttonPressed) {
  int itemCount = onlineMode ? ONLINE_MENU_ITEM_COUNT : OFFLINE_MENU_ITEM_COUNT;
  if (joyDir == JOY_UP) {
    moveMenuCursor(mainMenuCursor, mainMenuScrollOffset, -1, itemCount, MENU_VISIBLE_ITEMS);
  } else if (joyDir == JOY_DOWN) {
    moveMenuCursor(mainMenuCursor, mainMenuScrollOffset, 1, itemCount, MENU_VISIBLE_ITEMS);
  }

  if (buttonPressed) {
    selectMainMenuItem(mainMenuCursor);
  }
}

void selectMainMenuItem(int index) {
  if (onlineMode && index == 7) {
    if (syncPendingScores()) finishLogout();
    else { currentState = SYNC_ERROR; messageScreenStarted = millis(); }
    return;
  }
  switch (index) {
    case 0:  // Snake
      enterDifficultySelect(SNAKE);
      break;
    case 1:  // Pong
      activeGame = PONG;
      showHighScoreIntro(PONG);
      break;
    case 2:  // Space Invaders
      enterDifficultySelect(SPACE_INVADERS);
      break;
    case 3:  // Pac-Man
      enterDifficultySelect(PAC_MAN);
      break;
    case 4:  // Breakout
      enterBreakoutDifficultySelect();
      break;
    case 5:  // Tetris
      enterUnderConstruction();
      break;
    case 6:  // About
      currentState = ABOUT;
      break;
  }
}

void drawMainMenu() {
  int itemCount = onlineMode ? ONLINE_MENU_ITEM_COUNT : OFFLINE_MENU_ITEM_COUNT;
  drawMenuList(onlineMode ? "ONLINE HOME" : "OFFLINE HOME", mainMenuItems, itemCount,
               mainMenuCursor, mainMenuScrollOffset, MENU_VISIBLE_ITEMS);
}

// ============================================================
// DIFFICULTY SELECT — shared 3-option menu (Snake, Space Invaders, Pac-Man)
// ============================================================

void enterDifficultySelect(ConsoleState game) {
  pendingGameForDifficulty = game;
  difficultyCursor = (int)currentDifficulty;
  currentState = DIFFICULTY_SELECT;
}

void updateDifficultySelect(JoystickDir joyDir, bool buttonPressed) {
  int unusedScroll = 0;
  if (joyDir == JOY_UP) {
    moveMenuCursor(difficultyCursor, unusedScroll, -1, DIFFICULTY_ITEM_COUNT, DIFFICULTY_ITEM_COUNT);
  } else if (joyDir == JOY_DOWN) {
    moveMenuCursor(difficultyCursor, unusedScroll, 1, DIFFICULTY_ITEM_COUNT, DIFFICULTY_ITEM_COUNT);
  }

  if (buttonPressed) {
    currentDifficulty = (Difficulty)difficultyCursor;

    activeGame = pendingGameForDifficulty;
    showHighScoreIntro(pendingGameForDifficulty);
  }
}

void drawDifficultySelect() {
  drawMenuList("SELECT DIFFICULTY", difficultyItems, DIFFICULTY_ITEM_COUNT, difficultyCursor, 0, DIFFICULTY_ITEM_COUNT);
}

// ============================================================
// DIFFICULTY SELECT — Breakout-only 4-option menu
// ============================================================

void enterBreakoutDifficultySelect() {
  breakoutDifficultyCursor = 0;
  currentState = BREAKOUT_DIFFICULTY_SELECT;
}

void updateBreakoutDifficultySelect(JoystickDir joyDir, bool buttonPressed) {
  int unusedScroll = 0;
  if (joyDir == JOY_UP) {
    moveMenuCursor(breakoutDifficultyCursor, unusedScroll, -1, BREAKOUT_DIFFICULTY_ITEM_COUNT, BREAKOUT_DIFFICULTY_ITEM_COUNT);
  } else if (joyDir == JOY_DOWN) {
    moveMenuCursor(breakoutDifficultyCursor, unusedScroll, 1, BREAKOUT_DIFFICULTY_ITEM_COUNT, BREAKOUT_DIFFICULTY_ITEM_COUNT);
  }

  if (buttonPressed) {
    activeGame = BREAKOUT;
    showHighScoreIntro(BREAKOUT);
  }
}

void drawBreakoutDifficultySelect() {
  drawMenuList("SELECT DIFFICULTY", breakoutDifficultyItems, BREAKOUT_DIFFICULTY_ITEM_COUNT, breakoutDifficultyCursor, 0, BREAKOUT_DIFFICULTY_ITEM_COUNT);
}

// ============================================================
// ABOUT SCREEN
// ============================================================

void updateAboutScreen(bool buttonPressed) {
  if (buttonPressed) {
    currentState = MAIN_MENU;
  }
}

void drawAboutScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(44, 0);
  display.print(F("PS-67"));

  display.setCursor(40, 10);
  display.print(F("Version 1.0"));

  display.setCursor(22, 24);
  display.print(F("Designed by me"));

  display.setCursor(30, 40);
  display.print(F("Powered by"));

  display.setCursor(14, 50);
  display.print(F("Hopes and Prayers"));

  display.display();
}

// ============================================================
// UNDER CONSTRUCTION (shared placeholder for unfinished games)
// ============================================================

void enterUnderConstruction() {
  currentState = UNDER_CONSTRUCTION;
}

void updateUnderConstruction(bool buttonPressed) {
  if (buttonPressed) {
    currentState = MAIN_MENU;
  }
}

void drawUnderConstruction() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(48, 6);
  display.print(F("Sorry!"));

  display.setCursor(4, 24);
  display.print(F("Still Under"));

  display.setCursor(4, 34);
  display.print(F("Construction"));

  display.setCursor(60, 44);
  display.print(F(":)"));

  display.setCursor(20, 56);
  display.print(F("Press Button"));

  display.display();
}

// ============================================================
// PAUSE MENU (shared by Snake, Pong, Space Invaders, Pac-Man, Breakout)
// ============================================================

void updatePauseMenu(JoystickDir joyDir, bool buttonPressed) {
  int unusedScroll = 0;
  if (joyDir == JOY_UP) {
    moveMenuCursor(pauseMenuCursor, unusedScroll, -1, PAUSE_MENU_ITEM_COUNT, PAUSE_MENU_ITEM_COUNT);
  } else if (joyDir == JOY_DOWN) {
    moveMenuCursor(pauseMenuCursor, unusedScroll, 1, PAUSE_MENU_ITEM_COUNT, PAUSE_MENU_ITEM_COUNT);
  }

  if (buttonPressed) {
    switch (pauseMenuCursor) {
      case 0:  // Continue
        {
          unsigned long pausedDuration = millis() - pausedAt;
          if (activeGame == SNAKE) {
            snakeLastMoveTime += pausedDuration;
            currentState = SNAKE;
          } else if (activeGame == PONG) {
            pongLastMoveTime += pausedDuration;
            playerPaddleLastMoveTime += pausedDuration;
            aiLastMoveTime += pausedDuration;
            currentState = PONG;
          } else if (activeGame == SPACE_INVADERS) {
            siPlayerLastMoveTime += pausedDuration;
            siAlienLastMoveTime += pausedDuration;
            siAlienFireLastTime += pausedDuration;
            siPlayerBulletLastMoveTime += pausedDuration;
            siAlienBulletLastMoveTime += pausedDuration;
            currentState = SPACE_INVADERS;
          } else if (activeGame == PAC_MAN) {
            pacmanLastMoveTime += pausedDuration;
            ghostLastMoveTime[0] += pausedDuration;
            ghostLastMoveTime[1] += pausedDuration;
            if (pacmanLevelCompleteActive) pacmanLevelCompleteStart += pausedDuration;
            currentState = PAC_MAN;
          } else if (activeGame == BREAKOUT) {
            breakoutLastMoveTime += pausedDuration;
            breakoutPaddleLastMoveTime += pausedDuration;
            if (breakoutOverlayActive) breakoutOverlayStart += pausedDuration;
            currentState = BREAKOUT;
          }
        }
        break;

      case 1:  // Restart -> difficulty-supporting games go through their difficulty menu
        if (activeGame == SNAKE || activeGame == SPACE_INVADERS || activeGame == PAC_MAN) {
          enterDifficultySelect(activeGame);
        } else if (activeGame == BREAKOUT) {
          enterBreakoutDifficultySelect();
        } else if (activeGame == PONG) {
          restartPong();
          currentState = PONG;
        }
        break;

      case 2:  // Main Menu
        currentState = MAIN_MENU;
        break;
    }
  }
}

void drawPauseMenu() {
  drawMenuList("PAUSED", pauseMenuItems, PAUSE_MENU_ITEM_COUNT, pauseMenuCursor, 0, PAUSE_MENU_ITEM_COUNT);
}

// ============================================================
// GAME OVER SCREEN (shared by Snake, Pong, Space Invaders, Pac-Man, Breakout)
// ============================================================

void updateGameOverScreen(JoystickDir joyDir, bool buttonPressed) {
  int unusedScroll = 0;
  if (joyDir == JOY_UP) {
    moveMenuCursor(gameOverCursor, unusedScroll, -1, GAME_OVER_ITEM_COUNT, GAME_OVER_ITEM_COUNT);
  } else if (joyDir == JOY_DOWN) {
    moveMenuCursor(gameOverCursor, unusedScroll, 1, GAME_OVER_ITEM_COUNT, GAME_OVER_ITEM_COUNT);
  }

  if (buttonPressed) {
    if (gameOverCursor == 0) {  // Restart
      if (activeGame == SNAKE || activeGame == SPACE_INVADERS || activeGame == PAC_MAN) {
        enterDifficultySelect(activeGame);
      } else if (activeGame == BREAKOUT) {
        enterBreakoutDifficultySelect();
      } else if (activeGame == PONG) {
        restartPong();
        currentState = PONG;
      }
    } else {  // Main Menu
      currentState = MAIN_MENU;
    }
  }
}

void drawGameOverScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  char scoreBuf[4];

  if (activeGame == SNAKE) {
    display.setCursor(28, 0);
    display.print(F("GAME OVER"));

    display.setCursor(28, LINE_HEIGHT);
    display.print(F("Score: "));
    formatScoreThreeDigits(snakeScore, scoreBuf, sizeof(scoreBuf));
    display.print(scoreBuf);
  } else if (activeGame == SPACE_INVADERS) {
    display.setCursor(28, 0);
    display.print(F("GAME OVER"));

    display.setCursor(28, LINE_HEIGHT);
    display.print(F("Score: "));
    formatScoreThreeDigits(siScore, scoreBuf, sizeof(scoreBuf));
    display.print(scoreBuf);
  } else if (activeGame == PAC_MAN) {
    display.setCursor(28, 0);
    display.print(F("GAME OVER"));

    display.setCursor(28, LINE_HEIGHT);
    display.print(F("Score: "));
    formatScoreThreeDigits(pacmanScore, scoreBuf, sizeof(scoreBuf));
    display.print(scoreBuf);
  } else if (activeGame == BREAKOUT) {
    display.setCursor(28, 0);
    display.print(F("GAME OVER"));

    display.setCursor(28, LINE_HEIGHT);
    display.print(F("Score: "));
    formatScoreThreeDigits(breakoutScore, scoreBuf, sizeof(scoreBuf));
    display.print(scoreBuf);
  } else {  // PONG
    display.setCursor(24, 0);
    display.print(playerScore >= PONG_WIN_SCORE ? F("YOU WIN") : F("YOU LOSE"));

    display.setCursor(20, LINE_HEIGHT);
    display.print(F("Score: "));
    display.print(playerScore);
    display.print(F(" - "));
    display.print(aiScore);
  }

  int y = LINE_HEIGHT * 2 + 4;
  for (int i = 0; i < GAME_OVER_ITEM_COUNT; i++) {
    display.setCursor(0, y);
    display.print(i == gameOverCursor ? "> " : "  ");
    display.print(gameOverItems[i]);
    y += LINE_HEIGHT;
  }

  display.display();
}

// ============================================================
// SNAKE — GAME LOGIC
// ============================================================

unsigned long difficultyToSnakeSpeedMs(Difficulty d) {
  switch (d) {
    case DIFF_EASY:   return SNAKE_EASY_MS;
    case DIFF_MEDIUM: return SNAKE_MEDIUM_MS;
    case DIFF_HARD:   return SNAKE_HARD_MS;
  }
  return SNAKE_MEDIUM_MS;
}

void startSnake(Difficulty d) {
  snakeMoveIntervalMs = difficultyToSnakeSpeedMs(d);
  restartSnake();
}

void updateSnakeState(JoystickDir joyDir, bool buttonPressed) {
  Direction requested = snakeCurrentDirection;
  bool hasRequest = false;

  switch (joyDir) {
    case JOY_UP:    requested = DIR_UP;    hasRequest = true; break;
    case JOY_DOWN:  requested = DIR_DOWN;  hasRequest = true; break;
    case JOY_LEFT:  requested = DIR_LEFT;  hasRequest = true; break;
    case JOY_RIGHT: requested = DIR_RIGHT; hasRequest = true; break;
    case JOY_NONE:  hasRequest = false;    break;
  }

  if (hasRequest && !isOppositeDirection(requested, snakeCurrentDirection)) {
    snakePendingDirection = requested;
  }

  if (buttonPressed) {
    activeGame = SNAKE;
    pauseMenuCursor = 0;
    pausedAt = millis();
    currentState = PAUSED;
    return;
  }

  if (millis() - snakeLastMoveTime >= snakeMoveIntervalMs) {
    moveSnake();
    snakeLastMoveTime = millis();
  }
}

bool isOppositeDirection(Direction a, Direction b) {
  if (a == DIR_UP && b == DIR_DOWN) return true;
  if (a == DIR_DOWN && b == DIR_UP) return true;
  if (a == DIR_LEFT && b == DIR_RIGHT) return true;
  if (a == DIR_RIGHT && b == DIR_LEFT) return true;
  return false;
}

void moveSnake() {
  snakeCurrentDirection = snakePendingDirection;

  Point newHead = snakeBody[0];
  switch (snakeCurrentDirection) {
    case DIR_UP:    newHead.y -= 1; break;
    case DIR_DOWN:  newHead.y += 1; break;
    case DIR_LEFT:  newHead.x -= 1; break;
    case DIR_RIGHT: newHead.x += 1; break;
  }

  if (checkCollision(newHead)) {
    activeGame = SNAKE;
    gameOverCursor = 0;
    currentState = GAME_OVER;
    return;
  }

  bool ateFood = (newHead.x == snakeFood.x && newHead.y == snakeFood.y);
  int newLength = ateFood ? snakeLength + 1 : snakeLength;
  if (newLength > MAX_SNAKE_LENGTH) newLength = MAX_SNAKE_LENGTH;

  for (int i = newLength - 1; i > 0; i--) {
    snakeBody[i] = snakeBody[i - 1];
  }
  snakeBody[0] = newHead;
  snakeLength = newLength;

  if (ateFood) {
    snakeScore++;
    spawnFood();
  }
}

bool checkCollision(Point p) {
  if (p.x < 0 || p.x >= GRID_WIDTH || p.y < 0 || p.y >= GRID_HEIGHT) {
    return true;
  }

  for (int i = 0; i < snakeLength - 1; i++) {
    if (snakeBody[i].x == p.x && snakeBody[i].y == p.y) {
      return true;
    }
  }

  return false;
}

void spawnFood() {
  bool validPosition = false;

  while (!validPosition) {
    snakeFood.x = random(FOOD_EDGE_MARGIN, GRID_WIDTH - FOOD_EDGE_MARGIN);
    snakeFood.y = random(FOOD_EDGE_MARGIN, GRID_HEIGHT - FOOD_EDGE_MARGIN);

    validPosition = true;
    for (int i = 0; i < snakeLength; i++) {
      if (snakeBody[i].x == snakeFood.x && snakeBody[i].y == snakeFood.y) {
        validPosition = false;
        break;
      }
    }
  }
}

void restartSnake() {
  snakeLength = SNAKE_INITIAL_LENGTH;

  int startX = GRID_WIDTH / 2;
  int startY = GRID_HEIGHT / 2;

  for (int i = 0; i < snakeLength; i++) {
    snakeBody[i].x = startX - i;
    snakeBody[i].y = startY;
  }

  snakeCurrentDirection = DIR_RIGHT;
  snakePendingDirection = DIR_RIGHT;
  snakeScore = 0;
  joystickWaitingForCenter = false;
  snakeLastMoveTime = millis();

  spawnFood();
}

void drawSnakeScreen() {
  display.clearDisplay();
  drawSnakeSegments();
  drawCell(snakeFood.x, snakeFood.y);
  drawSnakeScore();
  display.display();
}

void drawSnakeSegments() {
  for (int i = 0; i < snakeLength; i++) {
    drawCell(snakeBody[i].x, snakeBody[i].y);
  }
  drawHollowSquare(snakeBody[0].x, snakeBody[0].y);
}

void drawSnakeScore() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Score: "));

  char scoreBuf[4];
  formatScoreThreeDigits(snakeScore, scoreBuf, sizeof(scoreBuf));
  display.print(scoreBuf);
}

// ============================================================
// PONG — GAME LOGIC
// ============================================================

void updatePong(JoystickDir joyDir, JoystickDir heldDir, bool buttonPressed) {
  if (buttonPressed) {
    activeGame = PONG;
    pauseMenuCursor = 0;
    pausedAt = millis();
    currentState = PAUSED;
    return;
  }

  if (millis() - playerPaddleLastMoveTime >= PADDLE_MOVE_MS) {
    if (heldDir == JOY_UP) {
      movePlayerPaddle(-1);
    } else if (heldDir == JOY_DOWN) {
      movePlayerPaddle(1);
    }
    playerPaddleLastMoveTime = millis();
  }

  if (millis() - pongLastMoveTime >= PONG_SPEED_MS) {
    moveBall();
    pongLastMoveTime = millis();
  }

  updateAI();
}

void movePlayerPaddle(int delta) {
  playerPaddleY += delta;
  if (playerPaddleY < 0) playerPaddleY = 0;
  if (playerPaddleY > GRID_HEIGHT - PADDLE_HEIGHT) playerPaddleY = GRID_HEIGHT - PADDLE_HEIGHT;
}

void updateAI() {
  if (millis() - aiLastMoveTime < AI_MOVE_MS) return;
  aiLastMoveTime = millis();

  if (ballDX <= 0) return;

  int paddleCenter = aiPaddleY + PADDLE_HEIGHT / 2;
  if (ballY < paddleCenter - AI_DEAD_ZONE) {
    aiPaddleY -= 1;
  } else if (ballY > paddleCenter + AI_DEAD_ZONE) {
    aiPaddleY += 1;
  }

  if (aiPaddleY < 0) aiPaddleY = 0;
  if (aiPaddleY > GRID_HEIGHT - PADDLE_HEIGHT) aiPaddleY = GRID_HEIGHT - PADDLE_HEIGHT;
}

bool checkPaddleCollision(int paddleX, int paddleY) {
  return (ballX == paddleX) && (ballY >= paddleY) && (ballY < paddleY + PADDLE_HEIGHT);
}

void moveBall() {
  ballX += ballDX;
  ballY += ballDY;

  if (ballY <= 0) {
    ballY = 0;
    ballDY = -ballDY;
  } else if (ballY >= GRID_HEIGHT - 1) {
    ballY = GRID_HEIGHT - 1;
    ballDY = -ballDY;
  }

  if (ballDX < 0 && checkPaddleCollision(PLAYER_PADDLE_X, playerPaddleY)) {
    ballDX = -ballDX;
  }

  if (ballDX > 0 && checkPaddleCollision(AI_PADDLE_X, aiPaddleY)) {
    ballDX = -ballDX;
  }

  if (ballX < 0) {
    aiScore++;
    if (aiScore >= PONG_WIN_SCORE) {
      enterPongGameOver();
    } else {
      resetBall(-1);
    }
    return;
  }

  if (ballX >= GRID_WIDTH) {
    playerScore++;
    if (playerScore >= PONG_WIN_SCORE) {
      enterPongGameOver();
    } else {
      resetBall(1);
    }
  }
}

void resetBall(int direction) {
  ballX = GRID_WIDTH / 2;
  ballY = GRID_HEIGHT / 2;
  ballDX = direction;
  ballDY = (random(0, 2) == 0) ? -1 : 1;
}

void enterPongGameOver() {
  activeGame = PONG;
  gameOverCursor = 0;
  currentState = GAME_OVER;
}

void startPong() {
  restartPong();
}

void restartPong() {
  playerScore = 0;
  aiScore = 0;

  playerPaddleY = (GRID_HEIGHT - PADDLE_HEIGHT) / 2;
  aiPaddleY = (GRID_HEIGHT - PADDLE_HEIGHT) / 2;

  unsigned long now = millis();
  pongLastMoveTime = now;
  playerPaddleLastMoveTime = now;
  aiLastMoveTime = now;

  resetBall((random(0, 2) == 0) ? -1 : 1);
}

void drawPong() {
  display.clearDisplay();
  drawPongPaddles();
  drawCell(ballX, ballY);
  drawPongScore();
  display.display();
}

void drawPongPaddles() {
  for (int i = 0; i < PADDLE_HEIGHT; i++) {
    drawCell(PLAYER_PADDLE_X, playerPaddleY + i);
    drawCell(AI_PADDLE_X, aiPaddleY + i);
  }
}

void drawPongScore() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(playerScore);

  display.setCursor(SCREEN_WIDTH - 12, 0);
  display.print(aiScore);
}

// ============================================================
// SPACE INVADERS — GAME LOGIC
// ============================================================

unsigned long difficultyToSIMoveMs(Difficulty d) {
  switch (d) {
    case DIFF_EASY:   return SI_EASY_MOVE_MS;
    case DIFF_MEDIUM: return SI_MEDIUM_MOVE_MS;
    case DIFF_HARD:   return SI_HARD_MOVE_MS;
  }
  return SI_MEDIUM_MOVE_MS;
}

unsigned long difficultyToSIFireMs(Difficulty d) {
  switch (d) {
    case DIFF_EASY:   return SI_EASY_FIRE_MS;
    case DIFF_MEDIUM: return SI_MEDIUM_FIRE_MS;
    case DIFF_HARD:   return SI_HARD_FIRE_MS;
  }
  return SI_MEDIUM_FIRE_MS;
}

void startSpaceInvaders(Difficulty d) {
  siMoveIntervalMs = difficultyToSIMoveMs(d);
  siFireIntervalMs = difficultyToSIFireMs(d);
  restartSpaceInvaders();
}

void restartSpaceInvaders() {
  siScore = 0;
  siLives = SI_MAX_LIVES;
  siPlayerX = GRID_WIDTH / 2;
  siPlayerY = GRID_HEIGHT - 1;

  spawnWave();

  unsigned long now = millis();
  siPlayerLastMoveTime = now;
  siAlienLastMoveTime = now;
  siAlienFireLastTime = now;
  siPlayerBulletLastMoveTime = now;
  siAlienBulletLastMoveTime = now;

  joystickWaitingForCenter = false;
  siButtonWasHeld = false;
  siLongPressHandled = false;
}

void spawnWave() {
  int startX = 11;
  int startY = 1;
  int index = 0;

  for (int row = 0; row < INVADER_ROWS; row++) {
    for (int col = 0; col < INVADER_COLS; col++) {
      invaders[index].x = startX + col * INVADER_SPACING;
      invaders[index].y = startY + row;
      invaders[index].alive = true;
      index++;
    }
  }

  siDirection = 1;
  siPlayerBulletActive = false;
  siAlienBulletActive = false;
}

void handleSpaceInvadersButton(bool buttonHeld) {
  if (buttonHeld && !siButtonWasHeld) {
    siButtonDownTime = millis();
    siLongPressHandled = false;
  }

  if (buttonHeld && !siLongPressHandled && (millis() - siButtonDownTime >= SI_PAUSE_HOLD_MS)) {
    siLongPressHandled = true;
    activeGame = SPACE_INVADERS;
    pauseMenuCursor = 0;
    pausedAt = millis();
    currentState = PAUSED;
    siButtonWasHeld = buttonHeld;
    return;
  }

  if (!buttonHeld && siButtonWasHeld && !siLongPressHandled) {
    fireSpaceInvadersBullet();
  }

  siButtonWasHeld = buttonHeld;
}

void updateSpaceInvaders(JoystickDir heldDir, bool buttonHeld) {
  handleSpaceInvadersButton(buttonHeld);
  if (currentState != SPACE_INVADERS) return;

  if (millis() - siPlayerLastMoveTime >= SI_PLAYER_MOVE_MS) {
    if (heldDir == JOY_LEFT) {
      moveSIPlayer(-1);
    } else if (heldDir == JOY_RIGHT) {
      moveSIPlayer(1);
    }
    siPlayerLastMoveTime = millis();
  }

  if (millis() - siAlienLastMoveTime >= siMoveIntervalMs) {
    moveAliens();
    siAlienLastMoveTime = millis();
  }

  if (siPlayerBulletActive && millis() - siPlayerBulletLastMoveTime >= SI_BULLET_SPEED_MS) {
    movePlayerBullet();
    siPlayerBulletLastMoveTime = millis();
  }

  if (!siAlienBulletActive && millis() - siAlienFireLastTime >= siFireIntervalMs) {
    trySpawnAlienBullet();
    siAlienFireLastTime = millis();
  }

  if (siAlienBulletActive && millis() - siAlienBulletLastMoveTime >= SI_BULLET_SPEED_MS) {
    moveAlienBullet();
    siAlienBulletLastMoveTime = millis();
  }

  checkCollisions();
  if (currentState != SPACE_INVADERS) return;

  if (allInvadersDead()) {
    spawnWave();
  }
}

void moveSIPlayer(int delta) {
  siPlayerX += delta;
  if (siPlayerX < 0) siPlayerX = 0;
  if (siPlayerX > GRID_WIDTH - 1) siPlayerX = GRID_WIDTH - 1;
}

void fireSpaceInvadersBullet() {
  if (siPlayerBulletActive) return;
  siPlayerBullet.x = siPlayerX;
  siPlayerBullet.y = siPlayerY - 1;
  siPlayerBulletActive = true;
}

void movePlayerBullet() {
  siPlayerBullet.y -= 1;
  if (siPlayerBullet.y < 0) {
    siPlayerBulletActive = false;
    return;
  }

  for (int i = 0; i < INVADER_COUNT; i++) {
    if (invaders[i].alive && invaders[i].x == siPlayerBullet.x && invaders[i].y == siPlayerBullet.y) {
      invaders[i].alive = false;
      siPlayerBulletActive = false;
      siScore++;
      break;
    }
  }
}

void trySpawnAlienBullet() {
  if (siAlienBulletActive) return;

  int aliveIndices[INVADER_COUNT];
  int aliveCount = 0;
  for (int i = 0; i < INVADER_COUNT; i++) {
    if (invaders[i].alive) {
      aliveIndices[aliveCount] = i;
      aliveCount++;
    }
  }
  if (aliveCount == 0) return;

  int chosen = aliveIndices[random(0, aliveCount)];
  siAlienBullet.x = invaders[chosen].x;
  siAlienBullet.y = invaders[chosen].y + 1;
  siAlienBulletActive = true;
}

void moveAlienBullet() {
  siAlienBullet.y += 1;
  if (siAlienBullet.y >= GRID_HEIGHT) {
    siAlienBulletActive = false;
  }
}

void moveAliens() {
  bool hitEdge = false;
  for (int i = 0; i < INVADER_COUNT; i++) {
    if (!invaders[i].alive) continue;
    int nextX = invaders[i].x + siDirection;
    if (nextX <= 0 || nextX >= GRID_WIDTH - 1) {
      hitEdge = true;
      break;
    }
  }

  if (hitEdge) {
    siDirection = -siDirection;
    for (int i = 0; i < INVADER_COUNT; i++) {
      if (invaders[i].alive) invaders[i].y += 1;
    }
  } else {
    for (int i = 0; i < INVADER_COUNT; i++) {
      if (invaders[i].alive) invaders[i].x += siDirection;
    }
  }
}

bool allInvadersDead() {
  for (int i = 0; i < INVADER_COUNT; i++) {
    if (invaders[i].alive) return false;
  }
  return true;
}

void checkCollisions() {
  if (siAlienBulletActive && siAlienBullet.x == siPlayerX && siAlienBullet.y == siPlayerY) {
    siAlienBulletActive = false;
    loseSILife();
    if (currentState != SPACE_INVADERS) return;
  }

  for (int i = 0; i < INVADER_COUNT; i++) {
    if (invaders[i].alive && invaders[i].y >= siPlayerY) {
      loseSILife();
      return;
    }
  }
}

void loseSILife() {
  siLives--;
  if (siLives <= 0) {
    activeGame = SPACE_INVADERS;
    gameOverCursor = 0;
    currentState = GAME_OVER;
  } else {
    spawnWave();
  }
}

void drawSpaceInvaders() {
  display.clearDisplay();
  drawSIInvaders();
  drawCell(siPlayerX, siPlayerY);

  if (siPlayerBulletActive) drawCell(siPlayerBullet.x, siPlayerBullet.y);
  if (siAlienBulletActive) drawCell(siAlienBullet.x, siAlienBullet.y);

  drawSIScore();
  drawSILives();
  display.display();
}

void drawSIInvaders() {
  for (int i = 0; i < INVADER_COUNT; i++) {
    if (invaders[i].alive) {
      drawCell(invaders[i].x, invaders[i].y);
    }
  }
}

void drawSIScore() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  char scoreBuf[4];
  formatScoreThreeDigits(siScore, scoreBuf, sizeof(scoreBuf));
  display.print(scoreBuf);
}

void drawSILives() {
  int heartSpacing = 10;
  int startX = SCREEN_WIDTH - (heartSpacing * SI_MAX_LIVES);

  for (int i = 0; i < SI_MAX_LIVES; i++) {
    bool filled = (i >= (SI_MAX_LIVES - siLives));
    drawHeart(startX + i * heartSpacing, 0, filled);
  }
}

// ============================================================
// PAC-MAN — MAZE
// ============================================================

const int TOP_ROW_WALL_COLS[] = { 1, 2, 4, 5, 10, 11, 13, 14, 28, 29 };
const int TOP_ROW_WALL_COL_COUNT = 10;

bool isTopRowWallColumn(int x) {
  for (int i = 0; i < TOP_ROW_WALL_COL_COUNT; i++) {
    if (TOP_ROW_WALL_COLS[i] == x) return true;
  }
  return false;
}

bool isWallCell(int x, int y) {
  if (x == 0 || x == GRID_WIDTH - 1 || y == 0 || y == GRID_HEIGHT - 1) return true;

  if (y == GRID_HEIGHT - 2) return false;

  if (y == 1) {
    return isTopRowWallColumn(x);
  }

  bool xInPillar = ((x - 1) % 3) < 2;
  bool yInPillar = ((y - 1) % 3) < 2;
  return xInPillar && yInPillar;
}

bool tryStep(int &x, int &y, Direction dir) {
  int nx = x;
  int ny = y;

  switch (dir) {
    case DIR_UP:    ny -= 1; break;
    case DIR_DOWN:  ny += 1; break;
    case DIR_LEFT:  nx -= 1; break;
    case DIR_RIGHT: nx += 1; break;
  }

  if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_HEIGHT) return false;
  if (isWallCell(nx, ny)) return false;

  x = nx;
  y = ny;
  return true;
}

Direction oppositeDirection(Direction d) {
  switch (d) {
    case DIR_UP:    return DIR_DOWN;
    case DIR_DOWN:  return DIR_UP;
    case DIR_LEFT:  return DIR_RIGHT;
    case DIR_RIGHT: return DIR_LEFT;
  }
  return d;
}

void initPellets() {
  pacmanPelletsRemaining = 0;
  for (int y = 0; y < GRID_HEIGHT; y++) {
    for (int x = 0; x < GRID_WIDTH; x++) {
      bool open = !isWallCell(x, y);
      bool onPelletParity = ((x + y) % 2) == 0;
      pelletPresent[y][x] = open && onPelletParity;
      if (pelletPresent[y][x]) pacmanPelletsRemaining++;
    }
  }
}

void consumePelletAt(int x, int y) {
  if (pelletPresent[y][x]) {
    pelletPresent[y][x] = false;
    pacmanScore++;
    pacmanPelletsRemaining--;
  }
}

// ============================================================
// PAC-MAN — GAME LOGIC
// ============================================================

unsigned long difficultyToGhostSpeedMs(Difficulty d) {
  switch (d) {
    case DIFF_EASY:   return PACMAN_GHOST_EASY_MS;
    case DIFF_MEDIUM: return PACMAN_GHOST_MEDIUM_MS;
    case DIFF_HARD:   return PACMAN_GHOST_HARD_MS;
  }
  return PACMAN_GHOST_MEDIUM_MS;
}

void startPacman(Difficulty d) {
  pacmanGhostMoveIntervalMs = difficultyToGhostSpeedMs(d);
  restartPacman();
}

void restartPacman() {
  pacmanScore = 0;
  pacmanLives = PACMAN_MAX_LIVES;
  initPellets();
  respawnPacmanAndGhosts();

  pacmanLevelCompleteActive = false;
  joystickWaitingForCenter = false;
}

void respawnPacmanAndGhosts() {
  pacmanX = PACMAN_SPAWN_X;
  pacmanY = PACMAN_SPAWN_Y;
  pacmanCurrentDir = DIR_LEFT;
  pacmanDesiredDir = DIR_LEFT;
  consumePelletAt(pacmanX, pacmanY);

  for (int i = 0; i < 2; i++) {
    ghostX[i] = GHOST_SPAWN_X[i];
    ghostY[i] = GHOST_SPAWN_Y[i];
    ghostDir[i] = (i == 0) ? DIR_UP : DIR_DOWN;
    ghostLastMoveTime[i] = millis();
  }

  pacmanLastMoveTime = millis();
}

void updatePacmanState(JoystickDir heldDir, bool buttonPressed) {
  if (buttonPressed) {
    activeGame = PAC_MAN;
    pauseMenuCursor = 0;
    pausedAt = millis();
    currentState = PAUSED;
    return;
  }

  if (pacmanLevelCompleteActive) {
    if (millis() - pacmanLevelCompleteStart >= PACMAN_LEVEL_COMPLETE_MS) {
      initPellets();
      respawnPacmanAndGhosts();
      pacmanLevelCompleteActive = false;
    }
    return;
  }

  switch (heldDir) {
    case JOY_UP:    pacmanDesiredDir = DIR_UP;    break;
    case JOY_DOWN:  pacmanDesiredDir = DIR_DOWN;  break;
    case JOY_LEFT:  pacmanDesiredDir = DIR_LEFT;  break;
    case JOY_RIGHT: pacmanDesiredDir = DIR_RIGHT; break;
    case JOY_NONE:  break;
  }

  if (millis() - pacmanLastMoveTime >= PACMAN_MOVE_MS) {
    movePacman();
    pacmanLastMoveTime = millis();
  }

  for (int i = 0; i < 2; i++) {
    if (millis() - ghostLastMoveTime[i] >= pacmanGhostMoveIntervalMs) {
      moveGhost(i);
      ghostLastMoveTime[i] = millis();
    }
  }

  checkGhostCollision();
  if (currentState != PAC_MAN) return;

  if (pacmanPelletsRemaining <= 0) {
    pacmanLevelCompleteActive = true;
    pacmanLevelCompleteStart = millis();
  }
}

void movePacman() {
  int testX = pacmanX;
  int testY = pacmanY;

  if (tryStep(testX, testY, pacmanDesiredDir)) {
    pacmanCurrentDir = pacmanDesiredDir;
    pacmanX = testX;
    pacmanY = testY;
  } else {
    testX = pacmanX;
    testY = pacmanY;
    if (tryStep(testX, testY, pacmanCurrentDir)) {
      pacmanX = testX;
      pacmanY = testY;
    }
  }

  consumePelletAt(pacmanX, pacmanY);
}

void moveGhost(int index) {
  Direction candidates[4];
  int candidateCount = 0;
  Direction reverse = oppositeDirection(ghostDir[index]);

  Direction allDirs[4] = { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
  for (int i = 0; i < 4; i++) {
    if (allDirs[i] == reverse) continue;
    int testX = ghostX[index];
    int testY = ghostY[index];
    if (tryStep(testX, testY, allDirs[i])) {
      candidates[candidateCount] = allDirs[i];
      candidateCount++;
    }
  }

  if (candidateCount == 0) {
    int testX = ghostX[index];
    int testY = ghostY[index];
    if (tryStep(testX, testY, reverse)) {
      candidates[0] = reverse;
      candidateCount = 1;
    }
  }

  if (candidateCount == 0) return;

  Direction chosen = (candidateCount == 1) ? candidates[0] : candidates[random(0, candidateCount)];

  int newX = ghostX[index];
  int newY = ghostY[index];
  tryStep(newX, newY, chosen);
  ghostX[index] = newX;
  ghostY[index] = newY;
  ghostDir[index] = chosen;
}

void checkGhostCollision() {
  for (int i = 0; i < 2; i++) {
    if (ghostX[i] == pacmanX && ghostY[i] == pacmanY) {
      pacmanLives--;
      if (pacmanLives <= 0) {
        activeGame = PAC_MAN;
        gameOverCursor = 0;
        currentState = GAME_OVER;
      } else {
        respawnPacmanAndGhosts();
      }
      return;
    }
  }
}

// ============================================================
// PAC-MAN — RENDERING
// ============================================================

void drawPacmanScreen() {
  display.clearDisplay();

  if (pacmanLevelCompleteActive) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(16, 24);
    display.print(F("LEVEL COMPLETE"));
    display.display();
    return;
  }

  drawMaze();
  drawHollowSquare(pacmanX, pacmanY);
  drawGhosts();
  drawPacmanHud();
  display.display();
}

void drawMaze() {
  for (int y = 0; y < GRID_HEIGHT; y++) {
    for (int x = 0; x < GRID_WIDTH; x++) {
      if (isWallCell(x, y)) {
        if (y == 0 || y == GRID_HEIGHT - 1) continue;
        drawCell(x, y);
      } else if (pelletPresent[y][x]) {
        int px = x * CELL_SIZE + CELL_SIZE / 2;
        int py = y * CELL_SIZE + CELL_SIZE / 2;
        display.drawPixel(px, py, SSD1306_WHITE);
      }
    }
  }
}

void drawGhosts() {
  bool blinkOn = ((millis() / GHOST_BLINK_INTERVAL_MS) % 2 == 0);
  for (int i = 0; i < 2; i++) {
    if (blinkOn) {
      drawHollowSquare(ghostX[i], ghostY[i]);
    } else {
      drawCell(ghostX[i], ghostY[i]);
    }
  }
}

void drawPacmanHud() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  char scoreBuf[4];
  formatScoreThreeDigits(pacmanScore, scoreBuf, sizeof(scoreBuf));
  display.print(scoreBuf);

  int heartSpacing = 10;
  int startX = SCREEN_WIDTH - (heartSpacing * PACMAN_MAX_LIVES);
  for (int i = 0; i < PACMAN_MAX_LIVES; i++) {
    bool filled = (i >= (PACMAN_MAX_LIVES - pacmanLives));
    drawHeart(startX + i * heartSpacing, 0, filled);
  }
}

// ============================================================
// BREAKOUT — GAME LOGIC
// ============================================================

unsigned long difficultyToBreakoutSpeedMs(Difficulty d) {
  switch (d) {
    case DIFF_EASY:   return BREAKOUT_EASY_MS;
    case DIFF_MEDIUM: return BREAKOUT_MEDIUM_MS;
    case DIFF_HARD:   return BREAKOUT_HARD_MS;
  }
  return BREAKOUT_MEDIUM_MS;
}

// Sets difficulty/mode, then does a full new-game reset via restartBreakout() —
// same start*/restart* split used by every other game in this console.
void startBreakout(Difficulty d, BreakoutMode mode) {
  breakoutMode = mode;
  breakoutDifficulty = d;
  breakoutMoveIntervalMs = difficultyToBreakoutSpeedMs(d);
  restartBreakout();
}

// Single source of truth for a brand-new Breakout game: score, lives,
// bricks, paddle, and ball all reset.
void restartBreakout() {
  breakoutScore = 0;
  breakoutLives = BREAKOUT_MAX_LIVES;
  breakoutOverlayActive = false;
  breakoutOverlayAction = BO_NONE;
  resetBricks();
  resetBreakoutPaddleAndBall();
  joystickWaitingForCenter = false;
}

// Rebuilds the brick grid only (used both for a new game and between
// stages/waves) — does not touch score or lives.
void resetBricks() {
  int totalWidth = (BREAKOUT_BRICK_COLS - 1) * BREAKOUT_BRICK_SPACING_X + 1;
  int startX = (GRID_WIDTH - totalWidth) / 2;
  int startY = 2;
  int index = 0;

  for (int row = 0; row < BREAKOUT_BRICK_ROWS; row++) {
    for (int col = 0; col < BREAKOUT_BRICK_COLS; col++) {
      bricks[index].x = startX + col * BREAKOUT_BRICK_SPACING_X;
      bricks[index].y = startY + row * BREAKOUT_BRICK_SPACING_Y;
      bricks[index].alive = true;
      index++;
    }
  }
}

// Resets paddle and ball to their starting positions only — does not
// touch bricks, score, or lives. Used after losing a life mid-round.
void resetBreakoutPaddleAndBall() {
  breakoutPaddleX = (GRID_WIDTH - BREAKOUT_PADDLE_WIDTH) / 2;

  breakoutBallX = GRID_WIDTH / 2;
  breakoutBallY = BREAKOUT_PADDLE_Y - 1;
  breakoutBallDX = (random(0, 2) == 0) ? -1 : 1;
  breakoutBallDY = -1;

  unsigned long now = millis();
  breakoutLastMoveTime = now;
  breakoutPaddleLastMoveTime = now;
}

bool allBricksDestroyed() {
  for (int i = 0; i < BREAKOUT_BRICK_COUNT; i++) {
    if (bricks[i].alive) return false;
  }
  return true;
}

void updateBreakoutState(JoystickDir heldDir, bool buttonPressed) {
  if (buttonPressed) {
    activeGame = BREAKOUT;
    pauseMenuCursor = 0;
    pausedAt = millis();
    currentState = PAUSED;
    return;
  }

  if (breakoutOverlayActive) {
    if (millis() - breakoutOverlayStart >= breakoutOverlayDurationMs) {
      resolveBreakoutOverlay();
    }
    return;  // freeze gameplay while an overlay message is showing
  }

  if (millis() - breakoutPaddleLastMoveTime >= BREAKOUT_PADDLE_MOVE_MS) {
    if (heldDir == JOY_LEFT) {
      moveBreakoutPaddle(-1);
    } else if (heldDir == JOY_RIGHT) {
      moveBreakoutPaddle(1);
    }
    breakoutPaddleLastMoveTime = millis();
  }

  if (millis() - breakoutLastMoveTime >= breakoutMoveIntervalMs) {
    moveBreakoutBall();
    breakoutLastMoveTime = millis();
  }

  if (currentState != BREAKOUT) return;  // moveBall() may have triggered Game Over

  if (allBricksDestroyed()) {
    triggerBreakoutLevelComplete();
  }
}

void moveBreakoutPaddle(int delta) {
  breakoutPaddleX += delta;
  if (breakoutPaddleX < 0) breakoutPaddleX = 0;
  if (breakoutPaddleX > GRID_WIDTH - BREAKOUT_PADDLE_WIDTH) breakoutPaddleX = GRID_WIDTH - BREAKOUT_PADDLE_WIDTH;
}

void moveBreakoutBall() {
  breakoutBallX += breakoutBallDX;
  breakoutBallY += breakoutBallDY;

  if (breakoutBallX <= 0) {
    breakoutBallX = 0;
    breakoutBallDX = 1;
  } else if (breakoutBallX >= GRID_WIDTH - 1) {
    breakoutBallX = GRID_WIDTH - 1;
    breakoutBallDX = -1;
  }

  if (breakoutBallY <= 0) {
    breakoutBallY = 0;
    breakoutBallDY = 1;
  }

  checkBrickCollision();
  checkPaddleCollision();

  if (breakoutBallY >= GRID_HEIGHT) {
    loseBreakoutLife();
  }
}

void checkBrickCollision() {
  for (int i = 0; i < BREAKOUT_BRICK_COUNT; i++) {
    if (bricks[i].alive && bricks[i].x == breakoutBallX && bricks[i].y == breakoutBallY) {
      bricks[i].alive = false;
      breakoutScore++;
      breakoutBallDY = -breakoutBallDY;
      break;  // only one brick can occupy a cell
    }
  }
}

// Divides the paddle into three regions: left third sends the ball left,
// middle sends it mostly straight up, right third sends it right.
void checkPaddleCollision() {
  if (breakoutBallDY <= 0) return;  // only relevant while moving downward
  if (breakoutBallY != BREAKOUT_PADDLE_Y) return;
  if (breakoutBallX < breakoutPaddleX || breakoutBallX >= breakoutPaddleX + BREAKOUT_PADDLE_WIDTH) return;

  int relativeHitPos = breakoutBallX - breakoutPaddleX;
  if (relativeHitPos <= 1) {
    breakoutBallDX = -1;  // left third
  } else if (relativeHitPos >= 3) {
    breakoutBallDX = 1;  // right third
  } else {
    breakoutBallDX = 0;  // middle third — mostly straight up
  }
  breakoutBallDY = -1;
}

void loseBreakoutLife() {
  breakoutLives--;
  if (breakoutLives <= 0) {
    activeGame = BREAKOUT;
    gameOverCursor = 0;
    currentState = GAME_OVER;
  } else {
    resetBreakoutPaddleAndBall();  // bricks and score stay exactly as they are
  }
}

// Decides what overlay message (and follow-up action) to show once the
// current brick set is cleared, based on mode and current stage.
void triggerBreakoutLevelComplete() {
  breakoutOverlayActive = true;
  breakoutOverlayStart = millis();

  if (breakoutMode == BREAKOUT_MODE_NORMAL) {
    breakoutOverlayAction = BO_RESET_SAME;
    breakoutOverlayDurationMs = BREAKOUT_LEVEL_COMPLETE_MS;
  } else {  // BREAKOUT_MODE_CHALLENGE
    if (breakoutDifficulty == DIFF_EASY) {
      breakoutOverlayAction = BO_ADVANCE_MEDIUM;
      breakoutOverlayDurationMs = BREAKOUT_LEVEL_COMPLETE_MS;
    } else if (breakoutDifficulty == DIFF_MEDIUM) {
      breakoutOverlayAction = BO_ADVANCE_HARD;
      breakoutOverlayDurationMs = BREAKOUT_LEVEL_COMPLETE_MS;
    } else {  // DIFF_HARD complete -> Challenge fully finished
      breakoutOverlayAction = BO_RETURN_MENU;
      breakoutOverlayDurationMs = BREAKOUT_FINAL_SCREEN_MS;
    }
  }
}

// Carries out whatever the overlay promised once its timer elapses.
void resolveBreakoutOverlay() {
  switch (breakoutOverlayAction) {
    case BO_RESET_SAME:
      resetBricks();
      resetBreakoutPaddleAndBall();
      break;

    case BO_ADVANCE_MEDIUM:
      breakoutDifficulty = DIFF_MEDIUM;
      breakoutMoveIntervalMs = difficultyToBreakoutSpeedMs(DIFF_MEDIUM);
      resetBricks();
      resetBreakoutPaddleAndBall();
      break;

    case BO_ADVANCE_HARD:
      breakoutDifficulty = DIFF_HARD;
      breakoutMoveIntervalMs = difficultyToBreakoutSpeedMs(DIFF_HARD);
      resetBricks();
      resetBreakoutPaddleAndBall();
      break;

    case BO_RETURN_MENU:
      currentState = MAIN_MENU;
      break;

    case BO_NONE:
      break;
  }

  breakoutOverlayActive = false;
  breakoutOverlayAction = BO_NONE;
}

// ============================================================
// BREAKOUT — RENDERING
// ============================================================

void drawBreakoutScreen() {
  display.clearDisplay();

  if (breakoutOverlayActive) {
    drawBreakoutOverlay();
    display.display();
    return;
  }

  drawBricks();
  drawCell(breakoutBallX, breakoutBallY);
  drawBreakoutPaddle();
  drawBreakoutHud();
  display.display();
}

void drawBricks() {
  for (int i = 0; i < BREAKOUT_BRICK_COUNT; i++) {
    if (bricks[i].alive) {
      drawCell(bricks[i].x, bricks[i].y);
    }
  }
}

void drawBreakoutPaddle() {
  for (int i = 0; i < BREAKOUT_PADDLE_WIDTH; i++) {
    drawCell(breakoutPaddleX + i, BREAKOUT_PADDLE_Y);
  }
}

void drawBreakoutHud() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  char scoreBuf[4];
  formatScoreThreeDigits(breakoutScore, scoreBuf, sizeof(scoreBuf));
  display.print(scoreBuf);

  int heartSpacing = 10;
  int startX = SCREEN_WIDTH - (heartSpacing * BREAKOUT_MAX_LIVES);
  for (int i = 0; i < BREAKOUT_MAX_LIVES; i++) {
    bool filled = (i >= (BREAKOUT_MAX_LIVES - breakoutLives));
    drawHeart(startX + i * heartSpacing, 0, filled);
  }
}

void drawBreakoutOverlay() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  switch (breakoutOverlayAction) {
    case BO_RESET_SAME:
      display.setCursor(28, 24);
      display.print(F("LEVEL COMPLETE"));
      break;

    case BO_ADVANCE_MEDIUM:
      display.setCursor(28, 20);
      display.print(F("LEVEL COMPLETE"));
      display.setCursor(16, 32);
      display.print(F("Medium Difficulty"));
      break;

    case BO_ADVANCE_HARD:
      display.setCursor(28, 20);
      display.print(F("LEVEL COMPLETE"));
      display.setCursor(24, 32);
      display.print(F("Hard Difficulty"));
      break;

    case BO_RETURN_MENU:
      display.setCursor(20, 4);
      display.print(F("DAMN..."));
      display.setCursor(8, 14);
      display.print(F("YOU REALLY"));
      display.setCursor(14, 24);
      display.print(F("ARE JOBLESS."));
      display.setCursor(38, 40);
      display.print(F("GAME"));
      display.setCursor(26, 50);
      display.print(F("COMPLETED!"));
      break;

    case BO_NONE:
      break;
  }
}