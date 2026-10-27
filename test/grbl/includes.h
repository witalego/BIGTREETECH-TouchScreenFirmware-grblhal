// Minimal stand-ins for the TFT firmware environment, used to unit test GrblControl.c on the host
#ifndef _TEST_INCLUDES_H_
#define _TEST_INCLUDES_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(x) (sizeof(x) / sizeof(x[0]))

typedef enum { FW_NOT_DETECTED, FW_MARLIN, FW_REPRAPFW, FW_KLIPPER, FW_SMOOTHIEWARE, FW_UNKNOWN, FW_GRBL } FW_TYPE;
typedef enum { X_AXIS, Y_AXIS, Z_AXIS, E_AXIS } AXIS;
typedef enum { PAUSE_NORMAL, PAUSE_M0, PAUSE_EXTERNAL } PAUSE_TYPE;
typedef enum { DIALOG_TYPE_INFO, DIALOG_TYPE_ERROR } DIALOG_TYPE;

typedef struct { bool connected; } HOST;
typedef struct { FW_TYPE firmwareType; } MACHINE_SETTINGS;

extern HOST infoHost;
extern MACHINE_SETTINGS infoMachineSettings;

#define SERIAL_PORT 0
#define SOUND_ERROR 0
#define BUZZER_PLAY(x)
#define MENU_IS_NOT(x) true

#define TASK_LOOP_WHILE(condition, ...) \
  while (condition)                     \
  {                                     \
    __VA_ARGS__;                        \
    loopProcessAndGUI();                \
  }

void Serial_Put(uint8_t port, const char * msg);
uint32_t OS_GetTimeMs(void);
void loopProcessAndGUI(void);
void coordinateSetAxisActual(AXIS axis, float position);
void speedSetCurrentPercent(uint8_t tool, uint16_t per);
bool isPrinting(void);
bool isPrintingFromTFT(void);
bool isPaused(void);
bool pausePrint(bool isPause, PAUSE_TYPE pauseType);
void abortPrint(void);
void setPrintAbort(void);
void InfoHost_Init(bool isConnected);
void InfoHost_UpdateListeningMode(void);
void clearCmdQueue(void);
bool storeCmd(const char * format, ...);
void setupMachine(FW_TYPE fwType);
void addNotification(DIALOG_TYPE style, const char * title, const char * text, bool drawDialog);

#endif
