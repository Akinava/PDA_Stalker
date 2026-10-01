#pragma once

// Connectivity refers to roads + power lines

enum ConnectivityMask
{
	RoadMask = 1,
	PowerlineMask = 2
};

// Connection map (2 bits per tile) is in EEPROM, not in RAM (RAM of ATmega328 is 2 KB (Arduboy has 2.5 KB)):
// after the saved city (EEPROM_STORAGE_SPACE_START + "CTY1" + GameState).
// It is the live map, so the city is saved when the map changes
#define CONNECTION_MAP_SIZE (MAP_WIDTH * MAP_HEIGHT / 4)
#define CONNECTION_MAP_ADDRESS (16 + 4 + sizeof(GameState))

// the map was changed after the last SaveCity
extern bool ConnectionsChanged;
void ClearConnections(void);

uint8_t GetConnections(int x, int y);
void SetConnections(int x, int y, uint8_t newVal);
void CalculatePowerConnectivity(void);
int GetConnectivityTileVariant(int x, int y, uint8_t mask);
bool IsSuitableForBridgedTile(int x, int y, uint8_t mask);
uint8_t* GetPowerGrid();
