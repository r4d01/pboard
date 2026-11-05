#ifndef INKVIEW_MOCK_H
#define INKVIEW_MOCK_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct {int unused;} ifont;
typedef struct {unsigned short width,height,depth,scanline;unsigned char data[];} ibitmap;
#define BLACK 0
#define WHITE 0xffffff
#define ALIGN_LEFT 0
#define VALIGN_TOP 0
#define KBD_URL 7
#define KBD_NUMERIC 4
#define KBD_TIME 9
#define EVT_INIT 21
#define EVT_EXIT 22
#define EVT_SHOW 23
#define EVT_HIDE 24
#define EVT_KEYPRESS 25
#define EVT_POINTERUP 29
#define IV_KEY_OK 0x0a
#define IV_KEY_UP 0x11
#define IV_KEY_DOWN 0x12
#define IV_KEY_MENU 0x17
#define IV_KEY_PREV 0x18
#define IV_KEY_NEXT 0x19
#define IV_KEY_HOME 0x1a
#define IV_KEY_BACK 0x1b
static char visible_text[131072];
static int mock_battery=87,mock_charging=0,mock_wifi=0,mock_net_result=0,mock_net_calls=0,mock_scale_fail=0,mock_load_fail=0;
static int mock_closed=0,mock_sleep_calls=0,mock_auto_off=1;
static int mock_usb=0,mock_sleep_elapsed=0,mock_timer_delay=0;
static time_t mock_time_offset=0;
static time_t mock_clock_time(void){return time(NULL)+mock_time_offset;}
static void (*mock_timer)(void);
static void (*mock_keyboard)(char *);
static int ScreenWidth(void){return 758;}
static int ScreenHeight(void){return 1024;}
static int GetBatteryPower(void){return mock_battery;}
static int IsCharging(void){return mock_charging;}
static void SetFont(ifont *f,int c){(void)f;(void)c;}
static char *DrawTextRect(int x,int y,int w,int h,const char *s,int flags) {
    (void)x;(void)y;(void)w;(void)h;(void)flags;
    if(strlen(visible_text)+strlen(s)+2<sizeof visible_text){strcat(visible_text,s);strcat(visible_text,"\n");}return NULL;
}
static void FillArea(int x,int y,int w,int h,int c){(void)x;(void)y;(void)w;(void)h;(void)c;}
static void DrawRect(int x,int y,int w,int h,int c){FillArea(x,y,w,h,c);}
static void DrawLine(int x,int y,int w,int h,int c){FillArea(x,y,w,h,c);}
static void PartialUpdate(int x,int y,int w,int h){(void)x;(void)y;(void)w;(void)h;}
static void WaitForUpdateComplete(void){}
static void FullUpdate(void){}
static void ClearScreen(void){}
static void DrawBitmap(int x,int y,const ibitmap *b){(void)x;(void)y;(void)b;}
static ibitmap *BitmapStretchCopy(const ibitmap *b,int x,int y,int w,int h,int nw,int nh) {
    (void)b;(void)x;(void)y;(void)w;(void)h;
    if(mock_scale_fail)return NULL;
    ibitmap *r=calloc(1,sizeof *r);r->width=nw;r->height=nh;return r;
}
static ibitmap *LoadBitmap(const char *path) {
    unsigned char h[54];FILE *f=fopen(path,"rb");if(!f)return NULL;
    int ok=fread(h,1,54,f)==54;fclose(f);if(!ok)return NULL;
    if(mock_load_fail)return NULL;
    ibitmap *r=calloc(1,sizeof *r);r->width=h[18]|(h[19]<<8);r->height=h[22]|(h[23]<<8);return r;
}
static void ClearTimer(void (*cb)(void)){if(mock_timer==cb)mock_timer=NULL;}
static void SetHardTimer(const char *name,void (*cb)(void),int ms){(void)name;mock_timer_delay=ms;mock_timer=cb;}
static void SetAutoPowerOff(int n){mock_auto_off=n;}
static void iv_sleepmode(int n){(void)n;}
static int GetSleepmode(void){return 1;}
static void SetOrientation(int n){(void)n;}
static int GetOrientation(void){return 0;}
static void SetPanelType(int n){(void)n;}
static int NetDisconnect(void){return 0;}
static int WiFiPower(int n){mock_wifi=n;return 0;}
static int IsUSBconnected(void){return mock_usb;}
static int QueryNetwork(void){return mock_wifi?2:0;}
static int NetConnect2(const char *n,int h){(void)n;(void)h;mock_net_calls++;return mock_net_result;}
static void OpenKeyboard(const char *t,char *b,int len,int f,void (*cb)(char *)){(void)t;(void)b;(void)len;(void)f;mock_keyboard=cb;}
static int GoSleep(int ms,int deep){(void)ms;(void)deep;mock_sleep_calls++;mock_time_offset+=mock_sleep_elapsed;return mock_sleep_elapsed?1:0;}
static ifont *OpenFont(const char *s,int size,int flags){(void)s;(void)size;(void)flags;return calloc(1,sizeof(ifont));}
static void CloseFont(ifont *f){free(f);}
static char *GetDeviceModel(void){return "HOST MOCK";}
static char *GetSoftwareVersion(void){return "MOCK";}
static void CloseApp(void){mock_closed=1;}
static void InkViewMain(int (*cb)(int,int,int)){cb(EVT_INIT,0,0);}
#endif
