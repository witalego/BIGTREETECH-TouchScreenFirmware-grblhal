#include "Cnc.h"
#include "includes.h"

#define CNC_REFRESH_MS      250    // screen refresh interval (same as the grbl status query interval)
#define CNC_SAFE_Z          5.0f   // work Z height for moves to the XY zero
#define CNC_PROBE_PLATE     0.0f   // default touch plate thickness
#define CNC_PROBE_DISTANCE  50     // max probing travel
#define CNC_PROBE_FEED      100    // probing feed rate
#define CNC_PROBE_RETRACT   5      // retract after probing
#define CNC_SPINDLE_RPM     10000  // default spindle speed
#define CNC_SPINDLE_RPM_MAX 30000
#define CNC_SPINDLE_STEP    1000

#define AXIS_CHAR(axis) ("XYZ"[axis])

// area above the 2nd row of icons, used for the position display (DRO)
#define DRO_X0 (rect_of_key[KEY_ICON_0].x0)
#define DRO_Y0 (rect_of_key[KEY_ICON_0].y0)
#define DRO_Y1 (rect_of_key[KEY_LABEL_0].y1)

#define DRO_LINE_HEIGHT LARGE_BYTE_HEIGHT

static char cncTitle[48];
static float cncPlateThickness = CNC_PROBE_PLATE;
static uint32_t cncSpindleRpm = CNC_SPINDLE_RPM;
static uint8_t cncJogStepIndex = 2;  // 1 mm

// cached values of the last drawn screen, to redraw only what changed
static float lastWPos[3];
static float lastMPos[3];
static char lastInfo[48];

static void cncDrawString(int16_t x, int16_t y, uint16_t color, uint16_t font, const char * str)
{
  setFontSize(font);
  GUI_SetTextMode(GUI_TEXTMODE_NORMAL);
  GUI_SetColor(color);
  GUI_SetBkColor(infoSettings.bg_color);
  GUI_DispString(x, y, (uint8_t *) str);
  setFontSize(FONT_SIZE_NORMAL);
  GUI_RestoreColorDefault();
}

static uint16_t cncStateColor(void)
{
  switch (grblGetState())
  {
    case GRBL_ALARM:
    case GRBL_DOOR:
      return MAT_RED;

    case GRBL_HOLD:
    case GRBL_TOOL:
      return MAT_YELLOW;

    case GRBL_RUN:
    case GRBL_JOG:
    case GRBL_HOME:
      return MAT_GREEN;

    default:
      return infoSettings.font_color;
  }
}

// title of the status screen: grbl state and active input signals
static bool cncUpdateTitle(void)
{
  char newTitle[sizeof(cncTitle)];

  if (!infoHost.connected)
    snprintf(newTitle, sizeof(newTitle), "%s", textSelect(LABEL_UNCONNECTED));
  else if (grblGetPins()[0] != '\0')
    snprintf(newTitle, sizeof(newTitle), "%s   Pn:%s", grblGetStateName(), grblGetPins());
  else
    snprintf(newTitle, sizeof(newTitle), "%s", grblGetStateName());

  if (strcmp(newTitle, cncTitle) == 0)
    return false;

  strcpy(cncTitle, newTitle);

  return true;
}

// work position in the title bar, right of the menu name
static void cncDrawTitlePos(void)
{
  if (getReminderStatus() != SYS_STATUS_IDLE || toastRunning())
    return;

  char tempstr[48];

  sprintf(tempstr, "X:%.2f  Y:%.2f  Z:%.2f    ", coordinateGetAxisActual(X_AXIS), coordinateGetAxisActual(Y_AXIS),
          coordinateGetAxisActual(Z_AXIS));

  GUI_SetTextMode(GUI_TEXTMODE_NORMAL);
  GUI_SetColor(infoSettings.status_color);
  GUI_SetBkColor(infoSettings.title_bg_color);
  GUI_DispLenString(rect_of_key[KEY_ICON_1].x0, (TITLE_END_Y - BYTE_HEIGHT) / 2, (uint8_t *) tempstr,
                    LCD_WIDTH - rect_of_key[KEY_ICON_1].x0, true);
  GUI_RestoreColorDefault();
}

// position display: work position (large font) and machine position for X, Y, Z
static void cncDrawAxes(int16_t x0, int16_t y0, int16_t x1, bool force)
{
  char tempstr[16];

  for (uint8_t i = 0; i < 3; i++)
  {
    float wpos = coordinateGetAxisActual(i);
    float mpos = grblGetMachinePos(i);
    int16_t y = y0 + i * DRO_LINE_HEIGHT;

    if (force)
    {
      tempstr[0] = AXIS_CHAR(i);
      tempstr[1] = '\0';
      cncDrawString(x0 + 4, y, infoSettings.status_color, FONT_SIZE_LARGE, tempstr);
    }

    if (force || wpos != lastWPos[i])
    {
      sprintf(tempstr, "%9.3f", wpos);
      cncDrawString(x0 + 8 + LARGE_BYTE_WIDTH, y, infoSettings.font_color, FONT_SIZE_LARGE, tempstr);
      lastWPos[i] = wpos;
    }

    if (force || mpos != lastMPos[i])
    {
      sprintf(tempstr, "M%9.3f", mpos);
      cncDrawString(x1 - 4 - 10 * BYTE_WIDTH, y + LARGE_BYTE_HEIGHT - BYTE_HEIGHT, MAT_SILVER, FONT_SIZE_NORMAL, tempstr);
      lastMPos[i] = mpos;
    }
  }
}

static void cncDrawStatusDro(bool force)
{
  int16_t x1 = rect_of_key[KEY_ICON_2].x1;
  char info[48];

  if (force)
    GUI_ClearRect(DRO_X0, DRO_Y0, x1, DRO_Y1);

  cncDrawAxes(DRO_X0, DRO_Y0 + 2, x1, force);

  snprintf(info, sizeof(info), "F%-5.0f S%-5lu  Feed%4u%%  ", grblGetFeedRate(), (unsigned long) grblGetSpindleSpeed(),
           grblGetOverride(GRBL_OV_FEED));

  if (force || strcmp(info, lastInfo) != 0)
  {
    cncDrawString(DRO_X0 + 4, DRO_Y1 - BYTE_HEIGHT - 2, infoSettings.font_color, FONT_SIZE_NORMAL, info);
    strcpy(lastInfo, info);
  }
}

static void cncSoftReset(void)
{
  grblEmergencyStop();
}

//
// Status screen
//

void menuCncStatus(void)
{
  MENUITEMS cncStatusItems = {
    // title
    {.address = cncTitle},
    // icon                          label
    {
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_CNC_RESET,               {.address = "Reset"}},
      {ICON_MOVE,                    {.address = "Jog"}},
      {ICON_CNC_ZERO_XYZ,            {.address = "Zero"}},
      {ICON_PRINT,                   {.address = "Files"}},
      {ICON_MAINMENU,                {.address = "Menu"}},
    }
  };

  KEY_VALUES key_num = KEY_IDLE;
  bool alarm = (grblGetState() == GRBL_ALARM);

  if (alarm)
  {
    cncStatusItems.items[KEY_ICON_3].icon = ICON_CNC_UNLOCK;
    cncStatusItems.items[KEY_ICON_3].label.address = "Unlock";
  }

  cncUpdateTitle();
  menuDrawPage(&cncStatusItems);
  cncDrawStatusDro(true);

  while (MENU_IS(menuCncStatus))
  {
    if (nextScreenUpdate(CNC_REFRESH_MS))
    {
      if (cncUpdateTitle())
        menuDrawTitle();

      cncDrawStatusDro(false);

      // the 4th icon unlocks an alarm, otherwise it is a soft reset
      if (alarm != (grblGetState() == GRBL_ALARM))
      {
        alarm = !alarm;
        cncStatusItems.items[KEY_ICON_3].icon = alarm ? ICON_CNC_UNLOCK : ICON_CNC_RESET;
        cncStatusItems.items[KEY_ICON_3].label.address = alarm ? "Unlock" : "Reset";
        menuDrawItem(&cncStatusItems.items[KEY_ICON_3], KEY_ICON_3);
      }
    }

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0:
      case KEY_ICON_1:
      case KEY_ICON_2:
      case KEY_LABEL_0:
      case KEY_LABEL_1:
      case KEY_LABEL_2:
      case KEY_ICON_4:
        OPEN_MENU(menuCncJog);
        break;

      case KEY_ICON_3:
        if (alarm)
          storeCmd("$X\n");
        else
          cncSoftReset();
        break;

      case KEY_ICON_5:
        OPEN_MENU(menuCncZero);
        break;

      case KEY_ICON_6:
        OPEN_MENU(menuPrint);
        break;

      case KEY_ICON_7:
        OPEN_MENU(menuCncMenu);
        break;

      default:
        break;
    }

    loopProcess();
  }
}

//
// Jog
//

static void cncJog(AXIS axis, float distance)
{
  if (GET_BIT(infoSettings.inverted_axis, axis))
    distance = -distance;

  storeCmd("$J=G91 G21 %c%.3f F%u\n", AXIS_CHAR(axis), distance,
           axis != Z_AXIS ? infoSettings.xy_speed[infoSettings.move_speed] : infoSettings.z_speed[infoSettings.move_speed]);
}

void menuCncJog(void)
{
  MENUITEMS cncJogItems = {
    // title
    {.address = "Jog"},
    // icon                          label
    {
      {ICON_X_INC,                   {.address = "X+"}},
      {ICON_Y_INC,                   {.address = "Y+"}},
      {ICON_Z_INC,                   {.address = "Z+"}},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_X_DEC,                   {.address = "X-"}},
      {ICON_Y_DEC,                   {.address = "Y-"}},
      {ICON_Z_DEC,                   {.address = "Z-"}},
      {ICON_BACK,                    LABEL_BACK},
    }
  };

  KEY_VALUES key_num = KEY_IDLE;
  float step = moveLenSteps[cncJogStepIndex];

  cncJogItems.items[KEY_ICON_3] = itemMoveLen[cncJogStepIndex];

  menuDrawPage(&cncJogItems);
  cncDrawTitlePos();

  while (MENU_IS(menuCncJog))
  {
    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0: cncJog(X_AXIS, step); break;
      case KEY_ICON_1: cncJog(Y_AXIS, step); break;
      case KEY_ICON_2: cncJog(Z_AXIS, step); break;

      case KEY_ICON_3:
        cncJogStepIndex = (cncJogStepIndex + 1) % ITEM_MOVE_LEN_NUM;
        cncJogItems.items[KEY_ICON_3] = itemMoveLen[cncJogStepIndex];
        menuDrawItem(&cncJogItems.items[KEY_ICON_3], KEY_ICON_3);
        step = moveLenSteps[cncJogStepIndex];
        break;

      case KEY_ICON_4: cncJog(X_AXIS, -step); break;
      case KEY_ICON_5: cncJog(Y_AXIS, -step); break;
      case KEY_ICON_6: cncJog(Z_AXIS, -step); break;

      case KEY_ICON_7:
        if (grblGetState() == GRBL_JOG)
          grblSendRealtime(GRBL_RT_JOG_CANCEL);

        CLOSE_MENU();
        break;

      default:
        break;
    }

    loopProcess();

    if (nextScreenUpdate(CNC_REFRESH_MS))
      cncDrawTitlePos();
  }
}

//
// Work zero and probing
//

static void cncProbeStart(void)
{
  storeCmd("G91 G38.2 Z-%u F%u\n", CNC_PROBE_DISTANCE, CNC_PROBE_FEED);
  storeCmd("G90 G10 L20 P0 Z%.3f\n", cncPlateThickness);
  storeCmd("G91 G0 Z%u\n", CNC_PROBE_RETRACT);
  storeCmd("G90\n");
}

static void cncProbe(void)
{
  static char msg[128];

  cncPlateThickness = numPadFloat("Touch plate thickness (mm)", cncPlateThickness, CNC_PROBE_PLATE, false);

  snprintf(msg, sizeof(msg), "Plate %.2f mm under the bit, clip attached?\nZ moves down max %u mm.",
           cncPlateThickness, CNC_PROBE_DISTANCE);

  popupDialog(DIALOG_TYPE_QUESTION, "Probe Z", msg, LABEL_CONFIRM, LABEL_CANCEL, cncProbeStart, NULL, NULL);
}

static void cncGotoXYZero(void)
{
  if (coordinateGetAxisActual(Z_AXIS) < CNC_SAFE_Z)
    storeCmd("G90 G0 Z%.1f\n", CNC_SAFE_Z);

  storeCmd("G90 G0 X0 Y0\n");
}

void menuCncZero(void)
{
  MENUITEMS cncZeroItems = {
    // title
    {.address = "Zero"},
    // icon                          label
    {
      {ICON_CNC_ZERO_X,              {.address = "Zero X"}},
      {ICON_CNC_ZERO_Y,              {.address = "Zero Y"}},
      {ICON_CNC_ZERO_Z,              {.address = "Zero Z"}},
      {ICON_CNC_ZERO_XYZ,            {.address = "Zero XYZ"}},
      {ICON_CNC_ZERO_XY,             {.address = "Zero XY"}},
      {ICON_CNC_GOTO_ZERO,           {.address = "Go to XY0"}},
      {ICON_CNC_PROBE,               {.address = "Probe Z"}},
      {ICON_BACK,                    LABEL_BACK},
    }
  };

  KEY_VALUES key_num = KEY_IDLE;

  menuDrawPage(&cncZeroItems);
  cncDrawTitlePos();

  while (MENU_IS(menuCncZero))
  {
    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0: storeCmd("G10 L20 P0 X0\n"); break;
      case KEY_ICON_1: storeCmd("G10 L20 P0 Y0\n"); break;
      case KEY_ICON_2: storeCmd("G10 L20 P0 Z0\n"); break;
      case KEY_ICON_3: storeCmd("G10 L20 P0 X0 Y0 Z0\n"); break;
      case KEY_ICON_4: storeCmd("G10 L20 P0 X0 Y0\n"); break;
      case KEY_ICON_5: cncGotoXYZero(); break;
      case KEY_ICON_6: cncProbe(); break;
      case KEY_ICON_7: CLOSE_MENU(); break;

      default:
        break;
    }

    loopProcess();

    if (nextScreenUpdate(CNC_REFRESH_MS))
      cncDrawTitlePos();
  }
}

//
// Spindle and overrides
//

static void cncSpindleTitle(char * title, uint8_t size)
{
  snprintf(title, size, "Spindle %s   S%lu   Ov %u%%", grblIsSpindleOn() ? "ON" : "OFF", (unsigned long) cncSpindleRpm,
           grblGetOverride(GRBL_OV_SPINDLE));
}

void menuCncSpindle(void)
{
  MENUITEMS cncSpindleItems = {
    // title
    {.address = cncTitle},
    // icon                          label
    {
      {ICON_CNC_SPINDLE_ON,          {.address = "ON"}},
      {ICON_CNC_SPINDLE_OFF,         {.address = "OFF"}},
      {ICON_DEC,                     {.address = "RPM -"}},
      {ICON_INC,                     {.address = "RPM +"}},
      {ICON_CNC_SPINDLE_DEC,         {.address = "-10%"}},
      {ICON_CNC_OV_RESET,            {.address = "100%"}},
      {ICON_CNC_SPINDLE_INC,         {.address = "+10%"}},
      {ICON_BACK,                    LABEL_BACK},
    }
  };

  KEY_VALUES key_num = KEY_IDLE;
  char title[sizeof(cncTitle)];

  cncSpindleTitle(cncTitle, sizeof(cncTitle));
  menuDrawPage(&cncSpindleItems);

  while (MENU_IS(menuCncSpindle))
  {
    key_num = menuKeyGetValue();

    // spindle on/off and speed changes would conflict with a running job, only overrides are allowed
    if (isPrinting() && key_num <= KEY_ICON_3)
    {
      addToast(DIALOG_TYPE_ERROR, "Not allowed while a job is running");
      key_num = KEY_IDLE;
    }

    switch (key_num)
    {
      case KEY_ICON_0:
        storeCmd("M3 S%lu\n", (unsigned long) cncSpindleRpm);
        break;

      case KEY_ICON_1:
        storeCmd("M5\n");
        break;

      case KEY_ICON_2:
      case KEY_ICON_3:
        if (key_num == KEY_ICON_2)
          cncSpindleRpm = (cncSpindleRpm > CNC_SPINDLE_STEP) ? cncSpindleRpm - CNC_SPINDLE_STEP : 0;
        else
          cncSpindleRpm = MIN(cncSpindleRpm + CNC_SPINDLE_STEP, CNC_SPINDLE_RPM_MAX);

        if (grblIsSpindleOn())
          storeCmd("S%lu\n", (unsigned long) cncSpindleRpm);
        break;

      case KEY_ICON_4: grblSendRealtime(GRBL_RT_SPINDLE_OV_MINUS10); break;
      case KEY_ICON_5: grblSendRealtime(GRBL_RT_SPINDLE_OV_RESET); break;
      case KEY_ICON_6: grblSendRealtime(GRBL_RT_SPINDLE_OV_PLUS10); break;
      case KEY_ICON_7: CLOSE_MENU(); break;

      default:
        break;
    }

    loopProcess();

    if (nextScreenUpdate(CNC_REFRESH_MS))
    {
      cncSpindleTitle(title, sizeof(title));

      if (strcmp(title, cncTitle) != 0)
      {
        strcpy(cncTitle, title);
        menuDrawTitle();
      }
    }
  }
}

//
// Main menu
//

void menuCncMenu(void)
{
  MENUITEMS cncMenuItems = {
    // title
    LABEL_MAINMENU,
    // icon                          label
    {
      {ICON_HOME,                    {.address = "Home ($H)"}},
      {ICON_CNC_UNLOCK,              {.address = "Unlock ($X)"}},
      {ICON_CNC_SPINDLE,             {.address = "Spindle"}},
      {ICON_CNC_PROBE,               {.address = "Probe Z"}},
      {ICON_GCODE,                   LABEL_TERMINAL},
      {ICON_CUSTOM,                  LABEL_CUSTOM},
      {ICON_SETTINGS,                LABEL_SETTINGS},
      {ICON_BACK,                    LABEL_BACK},
    }
  };

  KEY_VALUES key_num = KEY_IDLE;

  menuDrawPage(&cncMenuItems);

  while (MENU_IS(menuCncMenu))
  {
    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0: storeCmd("$H\n"); break;
      case KEY_ICON_1: storeCmd("$X\n"); break;
      case KEY_ICON_2: OPEN_MENU(menuCncSpindle); break;
      case KEY_ICON_3: cncProbe(); break;
      case KEY_ICON_4: OPEN_MENU(menuTerminal); break;
      case KEY_ICON_5: OPEN_MENU(menuCustom); break;
      case KEY_ICON_6: OPEN_MENU(menuSettings); break;
      case KEY_ICON_7: CLOSE_MENU(); break;

      default:
        break;
    }

    loopProcess();
  }
}

//
// Running job
//

static void cncDrawJobInfo(bool force)
{
  int16_t x1 = rect_of_key[KEY_ICON_3].x1;
  int16_t barY0 = DRO_Y0 + BYTE_HEIGHT + 2;
  int16_t barY1 = barY0 + 8;
  static uint8_t lastProgress;
  uint8_t progress = getPrintProgress();
  char info[48];
  char timeStr[12];

  if (force)
  {
    GUI_ClearRect(DRO_X0, DRO_Y0, x1, DRO_Y1);
    GUI_SetColor(PB_BORDER);
    GUI_DrawRect(DRO_X0, barY0, x1, barY1);
    GUI_RestoreColorDefault();
    lastProgress = 0xFF;
  }

  // state, progress, elapsed time, overrides
  time_2_string(timeStr, "%02u:%02u:%02u", getPrintTime());

  if (isPrinting())
    snprintf(info, sizeof(info), "%-5s %3u%%  %s  F%3u%% S%3u%%", grblGetStateName(), progress, timeStr,
             grblGetOverride(GRBL_OV_FEED), grblGetOverride(GRBL_OV_SPINDLE));
  else
    snprintf(info, sizeof(info), "%-9s %s", isAborted() ? "Aborted" : "Finished", timeStr);

  if (force || strcmp(info, lastInfo) != 0)
  {
    GUI_ClearRect(DRO_X0, DRO_Y0, x1, DRO_Y0 + BYTE_HEIGHT);
    cncDrawString(DRO_X0 + 4, DRO_Y0, isPrinting() ? cncStateColor() : infoSettings.font_color, FONT_SIZE_NORMAL, info);
    strcpy(lastInfo, info);
  }

  if (progress != lastProgress)
  {
    int16_t barX = DRO_X0 + 1 + (x1 - DRO_X0 - 2) * progress / 100;

    GUI_FillRectColor(DRO_X0 + 1, barY0 + 1, barX, barY1, PB_FILL);
    GUI_FillRectColor(barX, barY0 + 1, x1, barY1, PB_BCKG);
    lastProgress = progress;
  }

  // positions: the 3 axes take less than the full height, so use the area right of them for feed and spindle
  cncDrawAxes(DRO_X0, barY1 + 2, rect_of_key[KEY_ICON_2].x1, force);

  snprintf(info, sizeof(info), "F%-5.0f", grblGetFeedRate());
  cncDrawString(rect_of_key[KEY_ICON_3].x0, barY1 + 2 + DRO_LINE_HEIGHT, infoSettings.font_color, FONT_SIZE_NORMAL, info);
  snprintf(info, sizeof(info), "S%-5lu", (unsigned long) grblGetSpindleSpeed());
  cncDrawString(rect_of_key[KEY_ICON_3].x0, barY1 + 2 + 2 * DRO_LINE_HEIGHT, infoSettings.font_color, FONT_SIZE_NORMAL, info);
}

static inline void cncSetPauseItem(MENUITEMS * items, bool paused)
{
  items->items[KEY_ICON_4].icon = paused ? ICON_RESUME : ICON_PAUSE;
  items->items[KEY_ICON_4].label.index = paused ? LABEL_RESUME : LABEL_PAUSE;
}

void menuCncJob(void)
{
  MENUITEMS cncJobItems = {
    // title
    {.address = (void *) getPrintingTitle()},
    // icon                          label
    {
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_PAUSE,                   LABEL_PAUSE},
      {ICON_CNC_FEED_DEC,            {.address = "Feed -10%"}},
      {ICON_CNC_FEED_INC,            {.address = "Feed +10%"}},
      {ICON_STOP,                    LABEL_STOP},
    }
  };

  KEY_VALUES key_num = KEY_IDLE;
  bool lastPause = isPaused();
  bool lastPrinting = isPrinting();

  if (lastPrinting)
  {
    cncSetPauseItem(&cncJobItems, lastPause);
  }
  else  // returned to this menu after the job was done or aborted
  {
    cncJobItems.items[KEY_ICON_4] = (ITEM) {ICON_MAINMENU, {.index = LABEL_MAIN_SCREEN}};
    cncJobItems.items[KEY_ICON_5] = (ITEM) {ICON_NULL, {.index = LABEL_NULL}};
    cncJobItems.items[KEY_ICON_6] = (ITEM) {ICON_NULL, {.index = LABEL_NULL}};
    cncJobItems.items[KEY_ICON_7] = (ITEM) {ICON_BACK, {.index = LABEL_BACK}};
  }

  menuDrawPage(&cncJobItems);
  updatePrintProgress();
  cncDrawJobInfo(true);

  while (MENU_IS(menuCncJob))
  {
    // job just started or finished, restart the menu with the right icons
    if (lastPrinting != isPrinting())
      return;

    if (lastPause != isPaused())
    {
      lastPause = isPaused();
      cncSetPauseItem(&cncJobItems, lastPause);
      menuDrawItem(&cncJobItems.items[KEY_ICON_4], KEY_ICON_4);
    }

    if (nextScreenUpdate(CNC_REFRESH_MS))
    {
      updatePrintProgress();
      cncDrawJobInfo(false);
    }

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0:
      case KEY_ICON_1:
      case KEY_ICON_2:
      case KEY_ICON_3:
      case KEY_LABEL_0:
      case KEY_LABEL_1:
      case KEY_LABEL_2:
      case KEY_LABEL_3:
        OPEN_MENU(menuCncSpindle);  // spindle overrides
        break;

      case KEY_ICON_4:
        if (lastPrinting)
          pausePrint(!isPaused(), PAUSE_NORMAL);
        else
          infoMenu.cur = 0;  // home screen
        break;

      case KEY_ICON_5:
        if (lastPrinting)
          grblSendRealtime(GRBL_RT_FEED_OV_MINUS10);
        break;

      case KEY_ICON_6:
        if (lastPrinting)
          grblSendRealtime(GRBL_RT_FEED_OV_PLUS10);
        break;

      case KEY_ICON_7:
        if (lastPrinting)
        {
          popupDialog(DIALOG_TYPE_ALERT, LABEL_WARNING, "Stop the job?", LABEL_CONFIRM, LABEL_CANCEL, abortPrint, NULL, NULL);
        }
        else
        {
          // in case the job was started from menuPrintFromSource menu,
          // remove the filename from path to allow the files scanning from its folder avoiding a scanning error message
          exitFolder();
          CLOSE_MENU();
        }
        break;

      default:
        break;
    }

    loopProcess();
  }
}
