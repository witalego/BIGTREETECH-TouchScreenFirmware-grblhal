#ifndef _CNC_H_
#define _CNC_H_

#ifdef __cplusplus
extern "C" {
#endif

// CNC menus used when the TFT is connected to a grbl / grblHAL host (FW_GRBL)

// The CNC icons reuse the slots of 3D printer icons that are never shown with a grbl host
// (the external flash has no free space for new icons). The bitmaps in the "TFT35/bmp" folder
// of the grbl SD card package are replaced with the CNC images accordingly
#define ICON_CNC_ZERO_X      ICON_E_1_MM
#define ICON_CNC_ZERO_Y      ICON_E_5_MM
#define ICON_CNC_ZERO_Z      ICON_E_10_MM
#define ICON_CNC_ZERO_XYZ    ICON_E_100_MM
#define ICON_CNC_ZERO_XY     ICON_E_200_MM
#define ICON_CNC_GOTO_ZERO   ICON_E_1_PERCENT
#define ICON_CNC_FEED_DEC    ICON_E_5_PERCENT
#define ICON_CNC_FEED_INC    ICON_E_10_PERCENT
#define ICON_CNC_PROBE       ICON_BLTOUCH_DEPLOY
#define ICON_CNC_UNLOCK      ICON_BLTOUCH_STOW
#define ICON_CNC_SPINDLE     ICON_BLTOUCH_TEST
#define ICON_CNC_SPINDLE_ON  ICON_BLTOUCH_REPEAT
#define ICON_CNC_SPINDLE_OFF ICON_BLTOUCH_RESET
#define ICON_CNC_RESET       ICON_BLTOUCH
#define ICON_CNC_OV_RESET    ICON_1_DEGREE
#define ICON_CNC_SPINDLE_DEC ICON_5_DEGREE
#define ICON_CNC_SPINDLE_INC ICON_10_DEGREE

void menuCncStatus(void);   // home screen: position (DRO), state, feed, spindle
void menuCncMenu(void);     // main menu
void menuCncJog(void);
void menuCncZero(void);     // work zero and probing
void menuCncSpindle(void);  // spindle control and overrides
void menuCncJob(void);      // running job (replaces the Printing menu)

#ifdef __cplusplus
}
#endif

#endif
