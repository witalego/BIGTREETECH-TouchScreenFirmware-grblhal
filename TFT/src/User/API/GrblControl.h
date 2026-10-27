#ifndef _GRBL_CONTROL_H_
#define _GRBL_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// grbl / grblHAL host support.
// grbl uses a plain "ok" / "error:N" line protocol plus single byte real-time commands
// that are not acknowledged (status query, feed hold, cycle start, soft reset)

#define GRBL_RT_STATUS      "?"
#define GRBL_RT_FEED_HOLD   "!"
#define GRBL_RT_CYCLE_START "~"
#define GRBL_RT_RESET       "\x18"
#define GRBL_RT_JOG_CANCEL  "\x85"

// overrides (real-time commands)
#define GRBL_RT_FEED_OV_RESET      "\x90"
#define GRBL_RT_FEED_OV_PLUS10     "\x91"
#define GRBL_RT_FEED_OV_MINUS10    "\x92"
#define GRBL_RT_SPINDLE_OV_RESET   "\x99"
#define GRBL_RT_SPINDLE_OV_PLUS10  "\x9A"
#define GRBL_RT_SPINDLE_OV_MINUS10 "\x9B"

#define GRBL_STATUS_QUERY_MS 250   // status report polling interval
#define GRBL_STOP_TIMEOUT_MS 5000  // max time waiting for a feed hold to complete before a soft reset

typedef enum
{
  GRBL_UNKNOWN = 0,
  GRBL_IDLE,
  GRBL_RUN,
  GRBL_HOLD,
  GRBL_JOG,
  GRBL_ALARM,
  GRBL_DOOR,
  GRBL_CHECK,
  GRBL_HOME,
  GRBL_SLEEP,
  GRBL_TOOL,
} GRBL_STATE;

typedef enum
{
  GRBL_OV_FEED = 0,
  GRBL_OV_RAPID,
  GRBL_OV_SPINDLE,
} GRBL_OVERRIDE;

bool grblIsResponse(const char * line);  // check if the line is a typical grbl response (used for host detection)
void grblConnect(void);                  // set up the TFT for a grbl host
bool grblFilterCmd(char * cmd);          // translate/purge a gcode before sending it. Return "false" to purge it
void grblSendRealtime(const char * rtCmd);
void grblStatusQuery(void);
void grblParseStatus(const char * line);  // parse a status report ("<Idle|MPos:...|...>")
void grblHandleReset(void);               // handle the welcome message sent by grbl after a (soft) reset
void grblHandleAlarm(const char * line);  // handle "ALARM:N"
void grblHandleError(const char * line);  // handle "error:N"
GRBL_STATE grblGetState(void);
const char * grblGetStateName(void);
float grblGetMachinePos(uint8_t axis);  // machine position (MPos). The work position is in coordinateGetAxisActual()
float grblGetFeedRate(void);            // current feed rate (FS or F field)
uint32_t grblGetSpindleSpeed(void);     // current spindle speed (FS field)
bool grblIsSpindleOn(void);             // spindle state from the accessory field (A:S or A:C)
uint8_t grblGetOverride(GRBL_OVERRIDE ov);
const char * grblGetPins(void);         // triggered input signals (Pn field, e.g. "XYP"), empty string if none
void grblStopJob(void);                   // feed hold, wait for the motion to stop and then soft reset
void grblEmergencyStop(void);             // immediate soft reset

#ifdef __cplusplus
}
#endif

#endif
