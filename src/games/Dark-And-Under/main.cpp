// Arduino IDE builds the .ino files as one file: the main sketch, then the others
// in alphabetical order, and adds the prototypes of their functions. Here it is by hand
#include "Arduino.h"
#include "src/utils/Arduboy2Ext.h"
#include "Enums.h"
#include "src/levels/Level.h"
#include "images/Images.h"
#include "src/entities/Player.h"
#include "src/entities/Enemy.h"
#include "src/entities/Item.h"
#include "src/levels/MapData.h"
#include "src/controllers/PlayerController.h"
#include "src/controllers/EnemyController.h"
#include "src/fonts/Font3x5.h"
#include "src/utils/Utils.h"
#include "src/utils/EnemyNames.h"

// prototypes of the .ino functions
void setup();
void loop();
void drawFrames();
uint16_t displayLevelUp();
void displayNextLevel();
void displayEndOfGame(bool playerDead);
uint16_t battleLoop();
void damageEnemy(uint8_t attackingEnemyIdx, uint8_t hpLoss);
GameState battleEnemyAttacksInit(void);
GameState battleEnemyAttacks(void);
GameState battleEnemyDies(void);
GameState battlePlayerDecides(void);
GameState battlePlayerAttacks(void);
GameState battlePlayerDefends(void);
GameState battlePlayerCastsSpell(void);
void initialiseGame();
void initialiseLevel(Player *myHero, Level *myLevel, const uint8_t *level);
uint8_t loadItems(const uint8_t *level, Item * items, uint8_t idx, uint8_t max);
uint8_t loadEnemies(const uint8_t * level, Enemy * enemies, uint8_t idx, uint8_t max);
bool initEEPROM();
uint8_t getLevel();
void saveGame();
void restoreGame();
uint16_t inventoryLoop();
uint16_t itemLoop();
void displayLargeMap();
uint16_t playLoop();
void drawPlayerVision(Player *myHero, Level *myLevel);
void drawMapAndStatistics(Player *player, Level *myLevel, boolean smallMap);
void drawMapAndStatistics(Player *player, Level *myLevel);
void printStatistic(const __FlashStringHelper * str, const uint8_t stat);
void drawDirectionIndicator(Player *myHero);
void drawLevelDescription(Level *level);
void drawEnemyHitPointsBar(uint8_t hitPoints, uint8_t hitPointsMax);
void displaySplash();
void displayLogo();

#include "Dark-And-Under.ino"
#include "DarkUnder_Battle.ino"
#include "DarkUnder_Initialise.ino"
#include "DarkUnder_Inventory.ino"
#include "DarkUnder_Item.ino"
#include "DarkUnder_LargeMap.ino"
#include "DarkUnder_Play.ino"
#include "DarkUnder_Render.ino"

int main(void)
{
  setup();
  while(1) loop();
}
