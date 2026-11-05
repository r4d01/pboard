/* PocketBook Dashboard. See README.md for hardware validation requirements. */
#ifdef DASHBOARD_TEST
#include "tests/inkview_mock.h"
#else
#include <inkview.h>
#endif
#include <curl/curl.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "config.h"
#include "schedule.h"

#ifndef DASHBOARD_DIR
#define DASHBOARD_DIR "/mnt/ext1/applications/dashboard"
#endif
#define CFG DASHBOARD_DIR "/dashboard.cfg"
#define BMP DASHBOARD_DIR "/dashboard.bmp"
#define TEMP DASHBOARD_DIR "/download.bmp"
#define STATE DASHBOARD_DIR "/cache-state"
#define LOG DASHBOARD_DIR "/dashboard.log"
#define MAX_IMAGE (2u*1024u*1024u)
#define STATUS_HEIGHT 88
#define MAX_SHORT_SLEEPS 12
#define SLEEP_RETRY_MS 10000
#define NETWORK_SETTLE_MS 5000
#ifdef DASHBOARD_TEST
#define app_time() mock_clock_time()
#else
#define app_time() time(NULL)
#endif

enum { SETTINGS, RUNNING, PAUSED };
static dashboard_config cfg;
static int mode=SETTINGS, active=1, busy=0, keyboard_open=0, selected=0, editing=0;
static int old_sleep=1, old_orientation=0, failed_sleeps=0, battery=-1, charging=0;
static time_t next_update=0, last_success=0;
static ifont *small_font, *body_font, *title_font;
static ibitmap *screen_image, *fitted_image;
static int image_x, image_y;
static char status_text[384]="Ready", settings_error[256], edit_buffer[1024], previous_url[1024];
static void cycle(void);
static void sleep_until_update(void);

static void record(const char *format, ...) {
    struct stat st;
    if (!stat(LOG,&st) && st.st_size>128*1024) rename(LOG,DASHBOARD_DIR "/dashboard.previous.log");
    FILE *f=fopen(LOG,"a");
    if (!f) return;
    time_t now=app_time(); struct tm local; char stamp[64];
    localtime_r(&now,&local); strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S %Z",&local);
    fprintf(f,"%s battery=%d ",stamp,GetBatteryPower());
    va_list ap; va_start(ap,format); vfprintf(f,format,ap); va_end(ap);
    fputc('\n',f); fclose(f);
}
static void text_at(int x,int y,int w,int h,const char *s,ifont *font) {
    SetFont(font,BLACK); DrawTextRect(x,y,w,h,s,ALIGN_LEFT|VALIGN_TOP);
}
static void read_battery(void) { battery=GetBatteryPower(); charging=IsCharging(); }
static void battery_label(char *s,size_t n) {
    if (battery<0 || battery>100) snprintf(s,n,"Battery --");
    else snprintf(s,n,"Battery %d%%",battery);
}
static void format_time(time_t value,const char *format,char *s,size_t n) {
    if (!value) {snprintf(s,n,"--");return;}
    struct tm local; localtime_r(&value,&local); strftime(s,n,format,&local);
}
static void status_bar(int update) {
    int w=ScreenWidth(),y=ScreenHeight()-STATUS_HEIGHT;
    char label[64],last[40],next[40],line[160];
    read_battery(); battery_label(label,sizeof label);
    FillArea(0,y,w,STATUS_HEIGHT,WHITE); DrawLine(0,y,w-1,y,BLACK);
    text_at(12,y+6,w-184,49,status_text,small_font);
    text_at(w-166,y+6,158,26,label,small_font);
    text_at(w-166,y+32,158,26,charging?"Charging":"On battery",small_font);
    format_time(last_success,"%H:%M",last,sizeof last);
    format_time(next_update,"%d %b %H:%M",next,sizeof next);
    snprintf(line,sizeof line,"Last OK %s   Next %s   |   Tap for settings",last,next);
    text_at(12,y+60,w-24,26,line,small_font);
    if (update) {PartialUpdate(0,y,w,STATUS_HEIGHT);WaitForUpdateComplete();}
}
static void set_status(const char *format,...) {
    va_list ap; va_start(ap,format); vsnprintf(status_text,sizeof status_text,format,ap); va_end(ap);
    if (mode!=SETTINGS && active) status_bar(1);
}
static ibitmap *scale_image(ibitmap *src,int *x,int *y) {
    if (!src || !src->width || !src->height) return NULL;
    int aw=ScreenWidth(),ah=ScreenHeight()-STATUS_HEIGHT,w=aw,h=(int)src->height*aw/src->width;
    if (h>ah) {h=ah;w=(int)src->width*ah/src->height;}
    if (w<1 || h<1) return NULL;
    *x=(aw-w)/2; *y=(ah-h)/2;
    return BitmapStretchCopy(src,0,0,src->width,src->height,w,h);
}
static void draw_dashboard(void) {
    ClearScreen();
    if (fitted_image) DrawBitmap(image_x,image_y,fitted_image);
    else {
        text_at(28,60,ScreenWidth()-56,70,"Dashboard",title_font);
        text_at(28,150,ScreenWidth()-56,120,"No dashboard image yet. The last successful image will stay visible between updates.",body_font);
    }
    status_bar(0); FullUpdate(); WaitForUpdateComplete();
}
static void cancel_timers(void) {ClearTimer(cycle);ClearTimer(sleep_until_update);}
static void field(int index,int y,int height,const char *label,const char *value) {
    DrawRect(24,y,ScreenWidth()-48,height,BLACK);
    if (selected==index) DrawRect(27,y+3,ScreenWidth()-54,height-6,BLACK);
    text_at(40,y+8,ScreenWidth()-80,26,label,small_font);
    text_at(40,y+37,ScreenWidth()-80,height-42,value,body_font);
}
static void draw_settings(void) {
    char value[160]; ClearScreen(); read_battery();
    text_at(28,28,470,60,"Dashboard",title_font);
    battery_label(value,sizeof value); text_at(ScreenWidth()-180,35,170,30,value,small_font);
    text_at(ScreenWidth()-180,65,170,26,charging?"Charging":"On battery",small_font);
    text_at(28,96,ScreenWidth()-56,32,"Settings - tap a field to edit",body_font);
    field(0,140,138,"Remote image URL",cfg.url[0]?cfg.url:"http://server:8080/weather.bmp");
    snprintf(value,sizeof value,"%d minutes",cfg.interval_seconds/60); field(1,298,84,"Refresh every",value);
    snprintf(value,sizeof value,"%02d:%02d",cfg.quiet_start/60,cfg.quiet_start%60); field(2,402,84,"Quiet hours start",value);
    snprintf(value,sizeof value,"%02d:%02d",cfg.quiet_end/60,cfg.quiet_end%60); field(3,506,84,"Quiet hours end",value);
    char clock[80]; format_time(app_time(),"%d %b %H:%M %Z",clock,sizeof clock);
    snprintf(value,sizeof value,"Reader clock: %s",clock);text_at(28,610,ScreenWidth()-56,30,value,small_font);
    text_at(28,640,ScreenWidth()-56,60,"Quiet hours use this clock. Matching start and end times disable quiet hours.",small_font);
    DrawRect(24,716,ScreenWidth()-48,78,BLACK);
    if(selected==4)DrawRect(27,719,ScreenWidth()-54,72,BLACK);
    text_at(45,737,ScreenWidth()-90,40,"Save and start dashboard",body_font);
    text_at(28,820,ScreenWidth()-56,118,settings_error,body_font);
    text_at(28,958,ScreenWidth()-56,50,"Home / Back: exit    Page keys: select    OK: edit",small_font);
    FullUpdate();WaitForUpdateComplete();
}
static void show_settings(void) {
    cancel_timers();mode=SETTINGS;next_update=0;failed_sleeps=0;
    SetAutoPowerOff(1);iv_sleepmode(1);selected=cfg.url[0]?4:0;draw_settings();
}
static void radio_off(void) {
    int disconnect_result=NetDisconnect();
    int power_result=WiFiPower(0);
    record("network_off disconnect_result=%d wifi_power_result=%d network_flags=%d",
           disconnect_result,power_result,QueryNetwork());
}
static void start_dashboard(void) {
    if (!config_save(CFG,&cfg,settings_error,sizeof settings_error)) {draw_settings();return;}
    if (cfg.timezone[0]) {setenv("TZ",cfg.timezone,1);tzset();}
    if (strcmp(previous_url,cfg.url)) {
        free(fitted_image);fitted_image=NULL;free(screen_image);screen_image=NULL;
        unlink(BMP);unlink(STATE);last_success=0;
    }
    strcpy(previous_url,cfg.url);mode=RUNNING;SetAutoPowerOff(0);iv_sleepmode(1);
    failed_sleeps=0;next_update=0;set_status("Starting dashboard");draw_dashboard();
    SetHardTimer("dashboard",cycle,200);
}
static void keyboard_done(char *input) {
    keyboard_open=0;active=1;settings_error[0]=0;
    if(input) {
        int value;
        if(editing==0) {
            dashboard_config candidate=cfg;
            snprintf(candidate.url,sizeof candidate.url,"%s",input);
            if(config_valid(&candidate,settings_error,sizeof settings_error))strcpy(cfg.url,candidate.url);
        } else if(editing==1) {
            if(parse_integer(input,1,1440,&value))cfg.interval_seconds=value*60;
            else snprintf(settings_error,sizeof settings_error,"Enter 1 to 1440 whole minutes.");
        } else {
            if(parse_hhmm(input,&value)) {if(editing==2)cfg.quiet_start=value;else cfg.quiet_end=value;}
            else snprintf(settings_error,sizeof settings_error,"Use HH:MM, between 00:00 and 23:59.");
        }
    }
    draw_settings();
}
static void edit_field(int index) {
    selected=editing=index;
    if(index==4) {start_dashboard();return;}
    const char *title;int flags;
    if(index==0) {strcpy(edit_buffer,cfg.url);title="Remote image URL";flags=KBD_URL;}
    else if(index==1) {snprintf(edit_buffer,sizeof edit_buffer,"%d",cfg.interval_seconds/60);title="Refresh interval (minutes)";flags=KBD_NUMERIC;}
    else {
        int v=index==2?cfg.quiet_start:cfg.quiet_end;
        snprintf(edit_buffer,sizeof edit_buffer,"%02d:%02d",v/60,v%60);
        title=index==2?"Quiet hours start (HH:MM)":"Quiet hours end (HH:MM)";flags=KBD_TIME;
    }
    keyboard_open=1;OpenKeyboard(title,edit_buffer,sizeof edit_buffer-1,flags,keyboard_done);
}
typedef struct {FILE *file;size_t total;int too_large;} transfer;
static size_t receive_image(char *ptr,size_t size,size_t count,void *opaque) {
    transfer *t=opaque;
    if (size && count>MAX_IMAGE/size) {t->too_large=1;return 0;}
    size_t bytes=size*count;
    if(bytes>MAX_IMAGE-t->total) {t->too_large=1;return 0;}
    size_t written=fwrite(ptr,1,bytes,t->file);t->total+=written;return written;
}
static uint32_t le32(const unsigned char *b) {return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static int valid_bmp(const char *path) {
    unsigned char h[54];struct stat st;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int ok=fread(h,1,sizeof h,f)==sizeof h;fclose(f);
    if(!ok || stat(path,&st) || st.st_size>(off_t)MAX_IMAGE || h[0]!='B' || h[1]!='M')return 0;
    uint32_t w=le32(h+18),height=le32(h+22),offset=le32(h+10);
    unsigned depth=h[28]|(h[29]<<8);
    if(le32(h+14)!=40 || h[26]!=1 || h[27]!=0 || !w || w>2048 || !height || height>2048 ||
       (depth!=1 && depth!=8 && depth!=24) || le32(h+30)!=0)return 0;
    uint32_t palette=depth==24?0:le32(h+46);
    if(depth!=24 && !palette)palette=1u<<depth;
    if(palette>(1u<<depth) || offset<54+palette*4)return 0;
    uint32_t stride=((w*depth+31)/32)*4;
    return offset<=(uint32_t)st.st_size && stride*height<=(uint32_t)st.st_size-offset;
}
static void save_cache_state(void) {
    FILE *f=fopen(STATE ".tmp","w");if(!f)return;
    int ok=fprintf(f,"%ld\n%s\n",(long)last_success,cfg.url)>0;
    if(fclose(f))ok=0;
    if(ok)rename(STATE ".tmp",STATE);else unlink(STATE ".tmp");
}
static void restore_cache(void) {
    char url[1024],stamp[64];FILE *f=fopen(STATE,"r");if(!f)return;
    int ok=fgets(stamp,sizeof stamp,f)!=NULL && fgets(url,sizeof url,f)!=NULL;fclose(f);
    if(!ok)return;
    url[strcspn(url,"\r\n")]=0;
    if(strcmp(url,cfg.url)||!valid_bmp(BMP))return;
    screen_image=LoadBitmap(BMP);
    if(screen_image)fitted_image=scale_image(screen_image,&image_x,&image_y);
    if(fitted_image)last_success=(time_t)strtol(stamp,NULL,10);
}
static int fetch_image(void) {
    int ok=0;time_t started=app_time();
    /* Sleep is explicitly disabled during the transfer. Do not postpone the
       firmware power-off timer here: it may also delay a later suspend. */
    busy=1;iv_sleepmode(0);
    set_status("Connecting to Wi-Fi");WiFiPower(1);
    int net=NetConnect2(NULL,0);
    if(net) {set_status("Failed: Wi-Fi connection (code %d)",net);goto done;}
    set_status("Downloading image");
    FILE *f=fopen(TEMP,"wb");
    if(!f) {set_status("Failed: open download file: %s",strerror(errno));goto done;}
    CURL *curl=curl_easy_init();
    if(!curl) {fclose(f);set_status("Failed: cannot initialize downloader");goto done;}
    transfer t={f,0,0};
    curl_easy_setopt(curl,CURLOPT_URL,cfg.url);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"http,https");
#else
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,CURLPROTO_HTTP|CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl,CURLOPT_PROXY,"");
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,15L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive_image);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&t);
    CURLcode result=curl_easy_perform(curl);long code=0;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);
    if(t.too_large)set_status("Failed: image exceeds 2 MiB");
    else if(code && code!=200)set_status("Failed: HTTP %ld",code);
    else if(result!=CURLE_OK)set_status("Failed: %s (code %d)",curl_easy_strerror(result),(int)result);
    else if(code!=200 || t.total<54)set_status("Failed: empty or invalid image response");
    else ok=1;
    record("download http=%ld bytes=%lu curl=%d",code,(unsigned long)t.total,(int)result);
    curl_easy_cleanup(curl);
    if(fclose(f)) {ok=0;set_status("Failed: writing image: %s",strerror(errno));}
    if(ok) {
        set_status("Refreshing the dashboard");
        int valid=valid_bmp(TEMP);
        ibitmap *fresh=valid?LoadBitmap(TEMP):NULL;
        int x=0,y=0;ibitmap *scaled=fresh?scale_image(fresh,&x,&y):NULL;
        if(!fresh || !scaled) {
            const char *reason=!valid?"Failed: invalid or unsupported BMP":
                !fresh?"Failed: reader cannot decode BMP; try 8-bit grayscale":
                "Failed: cannot scale image (memory or firmware)";
            free(fresh);free(scaled);ok=0;set_status("%s",reason);
        }
        else if(rename(TEMP,BMP)) {free(fresh);free(scaled);ok=0;set_status("Failed: saving image: %s",strerror(errno));}
        else {
            free(screen_image);free(fitted_image);screen_image=fresh;fitted_image=scaled;image_x=x;image_y=y;
            last_success=app_time();save_cache_state();set_status("Finished OK");
        }
    }
done:
    unlink(TEMP);radio_off();iv_sleepmode(1);busy=0;
    record("refresh %s duration=%ld",status_text,(long)(app_time()-started));return ok;
}
static void sleep_until_update(void) {
    if(!active || mode!=RUNNING || busy)return;
    time_t now=app_time();long remaining=(long)(next_update-now);
    if(remaining<=0) {SetHardTimer("dashboard",cycle,100);return;}
    if(remaining>86400)remaining=86400;
    iv_sleepmode(1);
    int usb=IsUSBconnected(), sleepmode=GetSleepmode(), network=QueryNetwork();
    int result=GoSleep((int)(remaining*1000),0);
    long elapsed=(long)(app_time()-now);
    failed_sleeps=elapsed<2?failed_sleeps+1:0;
    record("sleep requested=%ld elapsed=%ld result=%d usb=%d sleepmode=%d network_flags=%d short_count=%d",
           remaining,elapsed,result,usb,sleepmode,network,failed_sleeps);
    if(failed_sleeps>=MAX_SHORT_SLEEPS) {
        mode=PAUSED;SetAutoPowerOff(1);next_update=0;
        set_status("Paused: sleep returned early repeatedly. Tap for settings; check dashboard.log.");return;
    }
    if(active && mode==RUNNING)SetHardTimer("dashboard",cycle,elapsed<2?SLEEP_RETRY_MS:200);
}
static void cycle(void) {
    if(!active || mode!=RUNNING || busy)return;
    time_t now=app_time();
    if(!next_update || now>=next_update) {
        if(!quiet_at_minutes(now,cfg.quiet_start,cfg.quiet_end)) {
            fetch_image();next_update=after_quiet_minutes(app_time()+cfg.interval_seconds,cfg.quiet_start,cfg.quiet_end);
        } else {
            radio_off();next_update=after_quiet_minutes(now,cfg.quiet_start,cfg.quiet_end);
            set_status("Quiet hours: keeping the last image");
        }
        if(active)draw_dashboard();
        record("next_update=%ld",(long)next_update);
        /* Give the firmware time to finish stopping Wi-Fi and drawing before sleep. */
        if(active && mode==RUNNING)SetHardTimer("dashboard-sleep",sleep_until_update,NETWORK_SETTLE_MS);
    } else sleep_until_update();
}
static int handler(int event,int p1,int p2) {
    switch(event) {
    case EVT_INIT:
        mkdir(DASHBOARD_DIR,0777);old_sleep=GetSleepmode();old_orientation=GetOrientation();
        SetOrientation(0);SetPanelType(0);
        small_font=OpenFont("LiberationSans",20,0);body_font=OpenFont("LiberationSans",26,0);title_font=OpenFont("LiberationSans",42,1);
        config_load(CFG,&cfg,settings_error,sizeof settings_error);strcpy(previous_url,cfg.url);
        if(cfg.timezone[0]) {setenv("TZ",cfg.timezone,1);tzset();}
        curl_global_init(CURL_GLOBAL_DEFAULT);restore_cache();
        record("start model=%s firmware=%s",GetDeviceModel(),GetSoftwareVersion());show_settings();return 1;
    case EVT_SHOW:
        active=1;
        if(!keyboard_open && !busy) {
            if(mode==SETTINGS)draw_settings();else {draw_dashboard();if(mode==RUNNING)SetHardTimer("dashboard",cycle,1500);}
        }
        return 1;
    case EVT_HIDE:active=0;cancel_timers();return 1;
    case EVT_POINTERUP:
        if(busy || keyboard_open)return 1;
        if(mode!=SETTINGS) {if(p2>=ScreenHeight()-STATUS_HEIGHT){settings_error[0]=0;show_settings();}return 1;}
        if(p1<24 || p1>ScreenWidth()-24)return 1;
        if(p2>=140 && p2<278)edit_field(0);
        else if(p2>=298 && p2<382)edit_field(1);
        else if(p2>=402 && p2<486)edit_field(2);
        else if(p2>=506 && p2<590)edit_field(3);
        else if(p2>=716 && p2<794)edit_field(4);
        return 1;
    case EVT_KEYPRESS:
        if(keyboard_open || busy)return 1;
        if(p1==IV_KEY_HOME || p1==IV_KEY_BACK) {CloseApp();return 1;}
        if(p1==IV_KEY_MENU && mode!=SETTINGS) {settings_error[0]=0;show_settings();return 1;}
        if(mode==SETTINGS) {
            if(p1==IV_KEY_NEXT || p1==IV_KEY_DOWN) {selected=(selected+1)%5;draw_settings();}
            else if(p1==IV_KEY_PREV || p1==IV_KEY_UP) {selected=(selected+4)%5;draw_settings();}
            else if(p1==IV_KEY_OK)edit_field(selected);
        }
        return 1;
    case EVT_EXIT:
        mode=PAUSED;cancel_timers();radio_off();SetAutoPowerOff(1);iv_sleepmode(old_sleep);SetOrientation(old_orientation);
        free(fitted_image);free(screen_image);CloseFont(small_font);CloseFont(body_font);CloseFont(title_font);curl_global_cleanup();return 1;
    default:return 0;
    }
}
int main(void) {InkViewMain(handler);return 0;}
