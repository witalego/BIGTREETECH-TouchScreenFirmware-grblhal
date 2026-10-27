#include "GrblControl.h"
#include "includes.h"

typedef struct
{
  const char * name;
  GRBL_STATE state;
} GRBL_STATE_NAME;

static const GRBL_STATE_NAME grblStateNames[] = {
  {"Idle",  GRBL_IDLE},
  {"Run",   GRBL_RUN},
  {"Hold",  GRBL_HOLD},
  {"Jog",   GRBL_JOG},
  {"Alarm", GRBL_ALARM},
  {"Door",  GRBL_DOOR},
  {"Check", GRBL_CHECK},
  {"Home",  GRBL_HOME},
  {"Sleep", GRBL_SLEEP},
  {"Tool",  GRBL_TOOL},
};

// short descriptions of grbl/grblHAL error codes (index = code - 1)
static const char * const grblErrorTexts[] = {
  "Expected command letter",                  // 1
  "Bad number format",                        // 2
  "Invalid $ statement",                      // 3
  "Negative value",                           // 4
  "Homing not enabled",                       // 5
  "Step pulse too short",                     // 6
  "Settings read fail",                       // 7
  "Not idle",                                 // 8
  "Locked out during alarm or jog",           // 9
  "Soft limits need homing enabled",          // 10
  "Line too long",                            // 11
  "Step rate too high",                       // 12
  "Safety door open",                         // 13
  "Line length exceeded",                     // 14
  "Jog travel exceeded",                      // 15
  "Invalid jog command",                      // 16
  "Laser mode requires PWM output",           // 17
  "",                                         // 18
  "",                                         // 19
  "Unsupported command",                      // 20
  "Modal group violation",                    // 21
  "Undefined feed rate",                      // 22
  "Integer value required",                   // 23
  "Axis command conflict",                    // 24
  "Repeated word",                            // 25
  "No axis words",                            // 26
  "Invalid line number",                      // 27
  "Value word missing",                       // 28
  "G59.x not supported",                      // 29
  "G53 requires G0 or G1",                    // 30
  "Axis words not used",                      // 31
  "Arc: no in-plane axis words",              // 32
  "Invalid target",                           // 33
  "Arc: invalid radius",                      // 34
  "Arc: no in-plane offset",                  // 35
  "Unused words",                             // 36
  "Tool length offset axis error",            // 37
  "Tool number too large",                    // 38
};

// short descriptions of grbl/grblHAL alarm codes (index = code - 1)
static const char * const grblAlarmTexts[] = {
  "Hard limit triggered",                     // 1
  "Soft limit: target exceeds travel",        // 2
  "Reset while in motion, re-home",           // 3
  "Probe fail: initial state",                // 4
  "Probe fail: no contact",                   // 5
  "Homing fail: reset",                       // 6
  "Homing fail: door opened",                 // 7
  "Homing fail: pull off",                    // 8
  "Homing fail: switch not found",            // 9
  "E-stop active",                            // 10
  "Homing required ($H)",                     // 11
  "Limit switch engaged",                     // 12
  "Probe protection triggered",               // 13
  "Spindle at speed timeout",                 // 14
  "Homing fail: auto squaring",               // 15
  "Self test failed",                         // 16
  "Motor fault",                              // 17
  "Homing fail",                              // 18
};

// Marlin-only M-codes (sent by TFT menus and queries) that grbl does not support
static const uint16_t marlinOnlyMCodes[] = {
  18, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 32, 33, 82, 83, 84, 92, 104, 105, 106, 107, 108, 109, 110,
  111, 114, 115, 117, 118, 119, 140, 150, 154, 155, 190, 211, 220, 221, 290, 300, 355, 401, 402, 420,
  500, 501, 502, 503, 524, 552, 600, 701, 702, 710, 851, 876,
};

static GRBL_STATE grblState = GRBL_UNKNOWN;
static bool grblHoldComplete = false;
static bool grblAbortRequest = false;  // abort the job from the main loop (not while parsing an ACK message)
static float grblWco[3] = {0.0f, 0.0f, 0.0f};
static float grblMPos[3] = {0.0f, 0.0f, 0.0f};
static float grblFeedRate = 0.0f;
static uint32_t grblSpindleSpeed = 0;
static bool grblSpindleOn = false;
static uint8_t grblOverride[3] = {100, 100, 100};
static char grblPins[16] = "";

bool grblIsResponse(const char * line)
{
  return (strncmp(line, "Grbl", 4) == 0 || strncmp(line, "error:", 6) == 0 || strncmp(line, "ALARM:", 6) == 0 ||
          strncmp(line, "[MSG:", 5) == 0 || line[0] == '<');
}

void grblConnect(void)
{
  setupMachine(FW_GRBL);
  InfoHost_Init(true);
  InfoHost_UpdateListeningMode();

  // the connection probe (M105) was rejected by grbl. grblHAL rejects all the following
  // gcode lines after an error until an empty line is received
  storeCmd("\n");
}

// "cmd" is the gcode without leading spaces and line number, terminated by "\n"
bool grblFilterCmd(char * cmd)
{
  char * end;

  if (cmd[0] == 'M')
  {
    uint16_t code = strtol(&cmd[1], &end, 10);

    if (*end == '.')  // e.g. M100.1, leave it to grbl
      return true;

    for (uint8_t i = 0; i < COUNT(marlinOnlyMCodes); i++)
    {
      if (marlinOnlyMCodes[i] == code)
        return false;
    }
  }
  else if (cmd[0] == 'G')
  {
    uint16_t code = strtol(&cmd[1], &end, 10);

    if (*end == '.')
      return true;

    if (code == 29)  // bed leveling
      return false;

    // G28 from TFT menus is a homing request. In a job file G28 is a regular grbl move to the stored position
    if (code == 28 && !isPrintingFromTFT())
    {
      char axes[4];
      uint8_t n = 0;

      for (const char * p = end; *p != '\0' && n < 3; p++)
      {
        if (*p == 'X' || *p == 'Y' || *p == 'Z')
          axes[n++] = *p;
      }

      axes[n] = '\0';
      sprintf(cmd, "$H%s\n", axes);  // "$H" homes all axes, "$HX" etc. needs single axis homing enabled in grblHAL
    }
  }

  return true;
}

void grblSendRealtime(const char * rtCmd)
{
  Serial_Put(SERIAL_PORT, rtCmd);
}

void grblStatusQuery(void)
{
  static uint32_t nextQueryTime = 0;

  if (grblAbortRequest)
  {
    grblAbortRequest = false;
    abortPrint();
  }

  if (!infoHost.connected || OS_GetTimeMs() < nextQueryTime)
    return;

  nextQueryTime = OS_GetTimeMs() + GRBL_STATUS_QUERY_MS;

  grblSendRealtime(GRBL_RT_STATUS);
}

// parse up to 3 comma separated values
static uint8_t parseAxes(const char * p, float * values)
{
  uint8_t n = 0;
  char * end;

  while (n < 3)
  {
    values[n++] = strtod((char *) p, &end);

    if (*end != ',')
      break;

    p = end + 1;
  }

  return n;
}

void grblParseStatus(const char * line)
{
  GRBL_STATE prevState = grblState;
  const char * p = line + 1;  // skip '<'
  float mpos[3];
  float wpos[3];
  bool hasMPos = false;
  bool hasWPos = false;

  grblState = GRBL_UNKNOWN;

  for (uint8_t i = 0; i < COUNT(grblStateNames); i++)
  {
    uint8_t len = strlen(grblStateNames[i].name);

    if (strncmp(p, grblStateNames[i].name, len) == 0)
    {
      grblState = grblStateNames[i].state;
      grblHoldComplete = (grblState == GRBL_HOLD && p[len] == ':' && p[len + 1] == '0');
      break;
    }
  }

  grblPins[0] = '\0';  // the Pn field is only sent when a signal is active

  // parse the fields
  for (p = strchr(p, '|'); p != NULL; p = strchr(p, '|'))
  {
    p++;

    if (strncmp(p, "MPos:", 5) == 0)
    {
      hasMPos = parseAxes(p + 5, mpos) == 3;
    }
    else if (strncmp(p, "WPos:", 5) == 0)
    {
      hasWPos = parseAxes(p + 5, wpos) == 3;
    }
    else if (strncmp(p, "WCO:", 4) == 0)
    {
      parseAxes(p + 4, grblWco);
    }
    else if (strncmp(p, "FS:", 3) == 0)
    {
      char * end;

      grblFeedRate = strtod((char *) p + 3, &end);

      if (*end == ',')
        grblSpindleSpeed = strtol(end + 1, NULL, 10);
    }
    else if (strncmp(p, "F:", 2) == 0)
    {
      grblFeedRate = strtod((char *) p + 2, NULL);
    }
    else if (strncmp(p, "Ov:", 3) == 0)
    {
      float ov[3];

      if (parseAxes(p + 3, ov) == 3)
      {
        for (uint8_t i = 0; i < 3; i++)
        {
          grblOverride[i] = ov[i];
        }
      }

      speedSetCurrentPercent(0, grblOverride[GRBL_OV_FEED]);

      grblSpindleOn = false;  // the accessory field (A:) follows the Ov field and is omitted when everything is off
    }
    else if (strncmp(p, "A:", 2) == 0)
    {
      for (p += 2; *p != '|' && *p != '>' && *p != '\0'; p++)
      {
        if (*p == 'S' || *p == 'C')
          grblSpindleOn = true;
      }
    }
    else if (strncmp(p, "Pn:", 3) == 0)
    {
      uint8_t n = 0;

      for (p += 3; *p != '|' && *p != '>' && *p != '\0' && n < sizeof(grblPins) - 1; p++)
      {
        grblPins[n++] = *p;
      }

      grblPins[n] = '\0';
    }
  }

  if (hasMPos)
  {
    for (uint8_t i = 0; i < 3; i++)
    {
      grblMPos[i] = mpos[i];
      wpos[i] = mpos[i] - grblWco[i];
    }

    hasWPos = true;
  }
  else if (hasWPos)
  {
    for (uint8_t i = 0; i < 3; i++)
    {
      grblMPos[i] = wpos[i] + grblWco[i];
    }
  }

  if (hasWPos)
  {
    coordinateSetAxisActual(X_AXIS, wpos[0]);
    coordinateSetAxisActual(Y_AXIS, wpos[1]);
    coordinateSetAxisActual(Z_AXIS, wpos[2]);
  }

  // keep the print status in sync with feed holds and cycle starts triggered outside of the TFT
  // (e.g. hardware buttons, M0 in the job file)
  if (isPrintingFromTFT())
  {
    if (grblState == GRBL_HOLD || grblState == GRBL_DOOR)
    {
      if (!isPaused())
        pausePrint(true, PAUSE_EXTERNAL);
    }
    else if ((prevState == GRBL_HOLD || prevState == GRBL_DOOR) && (grblState == GRBL_RUN || grblState == GRBL_IDLE))
    {
      if (isPaused())
        pausePrint(false, PAUSE_EXTERNAL);
    }
  }
}

void grblHandleReset(void)
{
  grblState = GRBL_UNKNOWN;

  if (isPrinting())
    setPrintAbort();

  // grbl flushed its buffers, so no "ok" will be received for pending gcodes
  InfoHost_Init(true);
  clearCmdQueue();
  InfoHost_UpdateListeningMode();
}

static void popupCode(const char * line, const char * const * texts, uint8_t textCount)
{
  char msg[64];
  uint8_t code = strtol(strchr(line, ':') + 1, NULL, 10);
  const char * text = (code >= 1 && code <= textCount) ? texts[code - 1] : "";

  snprintf(msg, sizeof(msg), "%.*s\n%s", (int) strcspn(line, "\r\n"), line, text);

  BUZZER_PLAY(SOUND_ERROR);
  addNotification(DIALOG_TYPE_ERROR, "grbl", msg, MENU_IS_NOT(menuTerminal));
}

void grblHandleAlarm(const char * line)
{
  grblState = GRBL_ALARM;

  if (isPrinting())
  {
    clearCmdQueue();
    setPrintAbort();
  }

  popupCode(line, grblAlarmTexts, COUNT(grblAlarmTexts));
}

void grblHandleError(const char * line)
{
  // grblHAL rejects all the following gcode lines after an error until an empty line or a '$' command is received
  if (isPrintingFromTFT())
  {
    if (grblAbortRequest)  // the job is already being aborted, don't flood with popups
      return;

    clearCmdQueue();
    grblAbortRequest = true;  // the rest of the job would be rejected, so stop it
  }
  else
  {
    // in alarm state all the gcodes are rejected (error:9), so drop the rest of a multi-line
    // sequence (e.g. probing after a probe fail) instead of showing a popup for each line
    if (grblState == GRBL_ALARM)
      clearCmdQueue();

    storeCmd("\n");  // unlock gcode processing for the next commands from the TFT menus
  }

  popupCode(line, grblErrorTexts, COUNT(grblErrorTexts));
}

GRBL_STATE grblGetState(void)
{
  return grblState;
}

const char * grblGetStateName(void)
{
  for (uint8_t i = 0; i < COUNT(grblStateNames); i++)
  {
    if (grblStateNames[i].state == grblState)
      return grblStateNames[i].name;
  }

  return "?";
}

float grblGetMachinePos(uint8_t axis)
{
  return grblMPos[axis];
}

float grblGetFeedRate(void)
{
  return grblFeedRate;
}

uint32_t grblGetSpindleSpeed(void)
{
  return grblSpindleSpeed;
}

bool grblIsSpindleOn(void)
{
  return grblSpindleOn;
}

uint8_t grblGetOverride(GRBL_OVERRIDE ov)
{
  return grblOverride[ov];
}

const char * grblGetPins(void)
{
  return grblPins;
}

void grblStopJob(void)
{
  uint32_t timeout = OS_GetTimeMs() + GRBL_STOP_TIMEOUT_MS;

  // a soft reset during motion loses the position (ALARM:3), so decelerate with a feed hold first
  grblSendRealtime(GRBL_RT_FEED_HOLD);

  TASK_LOOP_WHILE((grblState == GRBL_RUN || grblState == GRBL_JOG || grblState == GRBL_HOME ||
                   (grblState == GRBL_HOLD && !grblHoldComplete)) && OS_GetTimeMs() < timeout);

  grblSendRealtime(GRBL_RT_RESET);  // flush grbl buffers and stop the spindle
}

void grblEmergencyStop(void)
{
  grblSendRealtime(GRBL_RT_RESET);

  if (isPrinting())
  {
    clearCmdQueue();
    setPrintAbort();
  }
}
