// Host unit tests for TFT/src/User/API/GrblControl.c
// Build & run (from repo root): python -m ziglang cc -std=gnu11 -Wall -Itest/grbl -ITFT/src/User/API
//                               test/grbl/test_grbl.c TFT/src/User/API/GrblControl.c -o test_grbl.exe && test_grbl.exe
#include "includes.h"
#include "GrblControl.h"
#include <math.h>
#include <stdarg.h>

HOST infoHost = {true};
MACHINE_SETTINGS infoMachineSettings = {FW_GRBL};

// --- stubs and recorders ---
static char serialOut[256];
static uint32_t nowMs = 0;
static float axisPos[3];
static int feedOverride = -1;
static bool printing, paused, printingFromTFT;
static int pauseCalls, resumeCalls, abortCalls, setAbortCalls, clearQueueCalls, notifications;
static char lastStored[64];
static const char * feedStates[8];  // status reports fed during TASK_LOOP_WHILE
static int feedIndex;

void Serial_Put(uint8_t port, const char * msg) { strcat(serialOut, msg); }
uint32_t OS_GetTimeMs(void) { return nowMs; }
void loopProcessAndGUI(void)
{
  nowMs += 100;
  if (feedStates[feedIndex] != NULL)
    grblParseStatus(feedStates[feedIndex++]);
}
void coordinateSetAxisActual(AXIS axis, float position) { axisPos[axis] = position; }
void speedSetCurrentPercent(uint8_t tool, uint16_t per) { feedOverride = per; }
bool isPrinting(void) { return printing; }
bool isPrintingFromTFT(void) { return printingFromTFT; }
bool isPaused(void) { return paused; }
bool pausePrint(bool isPause, PAUSE_TYPE pauseType)
{
  if (isPause) pauseCalls++; else resumeCalls++;
  paused = isPause;
  return true;
}
void abortPrint(void) { abortCalls++; }
void setPrintAbort(void) { setAbortCalls++; printing = printingFromTFT = false; }
void InfoHost_Init(bool isConnected) { infoHost.connected = isConnected; }
void InfoHost_UpdateListeningMode(void) {}
void clearCmdQueue(void) { clearQueueCalls++; }
bool storeCmd(const char * format, ...)
{
  va_list va;
  va_start(va, format);
  vsnprintf(lastStored, sizeof(lastStored), format, va);
  va_end(va);
  return true;
}
void setupMachine(FW_TYPE fwType) { infoMachineSettings.firmwareType = fwType; }
void addNotification(DIALOG_TYPE style, const char * title, const char * text, bool drawDialog) { notifications++; }

// --- test helpers ---
static int failures = 0;

#define CHECK(cond)                                                  \
  do                                                                 \
  {                                                                  \
    if (!(cond))                                                     \
    {                                                                \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      failures++;                                                    \
    }                                                                \
  } while (0)

#define NEAR(a, b) (fabsf((a) - (b)) < 0.0005f)

static void reset(void)
{
  serialOut[0] = lastStored[0] = '\0';
  printing = paused = printingFromTFT = false;
  pauseCalls = resumeCalls = abortCalls = setAbortCalls = clearQueueCalls = notifications = 0;
  feedOverride = -1;
  feedIndex = 0;
  memset(feedStates, 0, sizeof(feedStates));
  grblHandleReset();  // reset grbl state
  clearQueueCalls = 0;
  infoHost.connected = true;
}

static bool filter(const char * in, const char * expected)  // expected NULL: purged
{
  char cmd[100];

  strcpy(cmd, in);

  bool keep = grblFilterCmd(cmd);

  if (expected == NULL)
    return !keep;

  if (!keep || strcmp(cmd, expected) != 0)
  {
    printf("  filter \"%s\" -> %s \"%s\"\n", in, keep ? "kept" : "purged", cmd);
    return false;
  }

  return true;
}

static void testDetection(void)
{
  CHECK(grblIsResponse("GrblHAL 1.1f ['$' or '$HELP' for help]\r\n"));
  CHECK(grblIsResponse("Grbl 1.1h ['$' for help]\r\n"));
  CHECK(grblIsResponse("error:20\r\n"));
  CHECK(grblIsResponse("ALARM:11\r\n"));
  CHECK(grblIsResponse("[MSG:'$H'|'$X' to unlock]\r\n"));
  CHECK(grblIsResponse("<Idle|MPos:0.000,0.000,0.000|FS:0,0>\r\n"));
  CHECK(!grblIsResponse("ok T:25.0 /0.0 B:24.0 /0.0 @:0 B@:0\n"));  // Marlin
  CHECK(!grblIsResponse("Error:Printer halted. kill() called!\n"));    // Marlin
  CHECK(!grblIsResponse("echo:Unknown command: \"?\"\n"));             // Marlin
}

static void testConnect(void)
{
  reset();
  infoHost.connected = false;
  infoMachineSettings.firmwareType = FW_NOT_DETECTED;
  grblConnect();
  CHECK(infoMachineSettings.firmwareType == FW_GRBL);
  CHECK(infoHost.connected);
  CHECK(strcmp(lastStored, "\n") == 0);  // unlock after the rejected M105 probe
}

static void testFilter(void)
{
  reset();
  CHECK(filter("M105\n", NULL));
  CHECK(filter("M114\n", NULL));
  CHECK(filter("M503 S0\n", NULL));
  CHECK(filter("M84\n", NULL));
  CHECK(filter("M220 S100\n", NULL));
  CHECK(filter("G29\n", NULL));
  CHECK(filter("M3 S1000\n", "M3 S1000\n"));
  CHECK(filter("M5\n", "M5\n"));
  CHECK(filter("M30\n", "M30\n"));
  CHECK(filter("M6 T1\n", "M6 T1\n"));
  CHECK(filter("M100.1\n", "M100.1\n"));
  CHECK(filter("G91\n", "G91\n"));
  CHECK(filter("G0 X10.00 F3000\n", "G0 X10.00 F3000\n"));
  CHECK(filter("G28.1\n", "G28.1\n"));
  CHECK(filter("G28\n", "$H\n"));
  CHECK(filter("G28 X\n", "$HX\n"));
  CHECK(filter("G28 Z\n", "$HZ\n"));
  CHECK(filter("G28 X Y\n", "$HXY\n"));
  CHECK(filter("$H\n", "$H\n"));
  CHECK(filter("$$\n", "$$\n"));
  CHECK(filter("!\n", "!\n"));
  CHECK(filter("\n", "\n"));

  printingFromTFT = printing = true;  // G28 in a job file is a regular grbl move
  CHECK(filter("G28 G91 Z0\n", "G28 G91 Z0\n"));
}

static void testStatus(void)
{
  reset();

  grblParseStatus("<Idle|MPos:10.000,20.500,-3.250|FS:0,0|WCO:1.000,2.000,3.000>\r\n");
  CHECK(grblGetState() == GRBL_IDLE);
  CHECK(strcmp(grblGetStateName(), "Idle") == 0);
  CHECK(NEAR(axisPos[0], 9.0f) && NEAR(axisPos[1], 18.5f) && NEAR(axisPos[2], -6.25f));

  // WCO is not sent in every report: keep using the last one
  grblParseStatus("<Run|MPos:11.000,20.500,-3.250|FS:500,0|Ov:120,100,100>\r\n");
  CHECK(grblGetState() == GRBL_RUN);
  CHECK(NEAR(axisPos[0], 10.0f));
  CHECK(feedOverride == 120);

  // WPos reporting ($10=0)
  grblParseStatus("<Jog|WPos:-1.500,2.000,0.125|FS:1000,0>\r\n");
  CHECK(grblGetState() == GRBL_JOG);
  CHECK(NEAR(axisPos[0], -1.5f) && NEAR(axisPos[2], 0.125f));

  // grblHAL extra fields and more axes
  grblParseStatus("<Hold:1|MPos:1.000,2.000,3.000,0.000|Bf:35,1023|FS:0,0|Pn:XYZ|A:S>\r\n");
  CHECK(grblGetState() == GRBL_HOLD);
  CHECK(NEAR(axisPos[2], 0.0f));  // 3.000 - WCO 3.000

  grblParseStatus("<Alarm|MPos:0.000,0.000,0.000|FS:0,0>\r\n");
  CHECK(grblGetState() == GRBL_ALARM);

  grblParseStatus("<Door:0|MPos:0.000,0.000,0.000|FS:0,0>\r\n");
  CHECK(grblGetState() == GRBL_DOOR);
}

static void testStatusData(void)
{
  reset();

  grblParseStatus("<Idle|MPos:10.000,20.500,-3.250|FS:0,0|WCO:1.000,2.000,3.000>\r\n");
  CHECK(NEAR(grblGetMachinePos(0), 10.0f) && NEAR(grblGetMachinePos(2), -3.25f));
  CHECK(grblGetPins()[0] == '\0');

  // feed, spindle speed, overrides and accessories
  grblParseStatus("<Run|MPos:11.000,20.500,-3.250|FS:1250.5,12000|Pn:XP|Ov:90,50,110|A:SF>\r\n");
  CHECK(NEAR(grblGetFeedRate(), 1250.5f));
  CHECK(grblGetSpindleSpeed() == 12000);
  CHECK(grblGetOverride(GRBL_OV_FEED) == 90 && grblGetOverride(GRBL_OV_RAPID) == 50 && grblGetOverride(GRBL_OV_SPINDLE) == 110);
  CHECK(feedOverride == 90);
  CHECK(grblIsSpindleOn());
  CHECK(strcmp(grblGetPins(), "XP") == 0);

  // Pn is omitted when no signal is active, overrides and accessories are kept until the next Ov field
  grblParseStatus("<Run|MPos:11.000,20.500,-3.250|FS:1000,12000>\r\n");
  CHECK(grblGetPins()[0] == '\0');
  CHECK(grblIsSpindleOn() && grblGetOverride(GRBL_OV_FEED) == 90);

  // Ov without A: everything off
  grblParseStatus("<Idle|MPos:11.000,20.500,-3.250|FS:0,0|Ov:100,100,100>\r\n");
  CHECK(!grblIsSpindleOn());
  CHECK(grblGetFeedRate() == 0.0f);

  // F field (no variable spindle) and WPos reporting: MPos from WCO
  grblParseStatus("<Jog|WPos:1.000,2.000,3.000|F:800>\r\n");
  CHECK(NEAR(grblGetFeedRate(), 800.0f));
  CHECK(NEAR(grblGetMachinePos(0), 2.0f) && NEAR(grblGetMachinePos(1), 4.0f) && NEAR(grblGetMachinePos(2), 6.0f));

  // grblHAL may send an empty accessory field
  grblParseStatus("<Idle|MPos:0,0,0|FS:0,0|Ov:100,100,100|A:|TLR:0>\r\n");
  CHECK(!grblIsSpindleOn());
}

static void testPauseSync(void)
{
  reset();
  printing = printingFromTFT = true;

  grblParseStatus("<Run|MPos:0,0,0|FS:500,0>\r\n");
  CHECK(pauseCalls == 0 && resumeCalls == 0);

  // feed hold from a hardware button or M0 in the file
  grblParseStatus("<Hold:1|MPos:0,0,0|FS:0,0>\r\n");
  CHECK(pauseCalls == 1 && paused);
  grblParseStatus("<Hold:0|MPos:0,0,0|FS:0,0>\r\n");
  CHECK(pauseCalls == 1);  // no repeated pause

  // cycle start from a hardware button
  grblParseStatus("<Run|MPos:0,0,0|FS:500,0>\r\n");
  CHECK(resumeCalls == 1 && !paused);

  // a stale "Run" report received right after the TFT sent a feed hold must not resume the job
  paused = true;
  grblParseStatus("<Run|MPos:0,0,0|FS:500,0>\r\n");
  CHECK(resumeCalls == 1 && paused);
}

static void testErrors(void)
{
  // error while not printing: popup and unlock grblHAL with an empty line
  reset();
  grblHandleError("error:20\r\n");
  CHECK(notifications == 1);
  CHECK(strcmp(lastStored, "\n") == 0);
  CHECK(abortCalls == 0);
  CHECK(clearQueueCalls == 0);

  // error in alarm state: drop the rest of a multi-line sequence
  reset();
  grblHandleAlarm("ALARM:5\r\n");
  grblHandleError("error:9\r\n");
  CHECK(clearQueueCalls == 1);
  CHECK(strcmp(lastStored, "\n") == 0);

  // error during a job: abort once, from the main loop
  reset();
  printing = printingFromTFT = true;
  grblHandleError("error:33\r\n");
  grblHandleError("error:33\r\n");  // following lines rejected by grblHAL
  CHECK(notifications == 1);
  CHECK(clearQueueCalls == 1);
  CHECK(abortCalls == 0);
  grblStatusQuery();
  CHECK(abortCalls == 1);
  grblStatusQuery();
  CHECK(abortCalls == 1);

  // alarm during a job
  reset();
  printing = printingFromTFT = true;
  grblHandleAlarm("ALARM:1\r\n");
  CHECK(grblGetState() == GRBL_ALARM);
  CHECK(setAbortCalls == 1 && notifications == 1);

  // unexpected reset during a job
  reset();
  printing = printingFromTFT = true;
  grblHandleReset();
  CHECK(setAbortCalls == 1);
  CHECK(clearQueueCalls == 1);
}

static void testStatusQuery(void)
{
  reset();
  nowMs = 1000;
  grblStatusQuery();
  CHECK(strcmp(serialOut, "?") == 0);
  grblStatusQuery();  // too early
  CHECK(strcmp(serialOut, "?") == 0);
  nowMs += GRBL_STATUS_QUERY_MS;
  grblStatusQuery();
  CHECK(strcmp(serialOut, "??") == 0);

  serialOut[0] = '\0';
  infoHost.connected = false;
  nowMs += GRBL_STATUS_QUERY_MS;
  grblStatusQuery();
  CHECK(serialOut[0] == '\0');
}

static void testStopJob(void)
{
  // running: feed hold, wait for the hold to complete, then reset
  reset();
  grblParseStatus("<Run|MPos:0,0,0|FS:500,0>\r\n");
  feedStates[0] = "<Run|MPos:0,0,0|FS:500,0>\r\n";
  feedStates[1] = "<Hold:1|MPos:0,0,0|FS:100,0>\r\n";
  feedStates[2] = "<Hold:0|MPos:0,0,0|FS:0,0>\r\n";
  nowMs = 0;
  grblStopJob();
  CHECK(strcmp(serialOut, "!\x18") == 0);
  CHECK(feedIndex == 3);
  CHECK(nowMs < GRBL_STOP_TIMEOUT_MS);

  // no status reports: reset after the timeout
  reset();
  grblParseStatus("<Run|MPos:0,0,0|FS:500,0>\r\n");
  nowMs = 0;
  grblStopJob();
  CHECK(strcmp(serialOut, "!\x18") == 0);
  CHECK(nowMs >= GRBL_STOP_TIMEOUT_MS);

  // idle: reset immediately
  reset();
  grblParseStatus("<Idle|MPos:0,0,0|FS:0,0>\r\n");
  nowMs = 0;
  grblStopJob();
  CHECK(strcmp(serialOut, "!\x18") == 0);
  CHECK(nowMs == 0);
}

int main(void)
{
  testDetection();
  testConnect();
  testFilter();
  testStatus();
  testStatusData();
  testPauseSync();
  testErrors();
  testStatusQuery();
  testStopJob();

  printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
  return failures != 0;
}
