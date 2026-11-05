#include <assert.h>
#define DASHBOARD_TEST
#define DASHBOARD_DIR "test-device"
#define main dashboard_main
#include "../dashboard.c"
#undef main
static void url_for(const char *base,const char *path) {snprintf(cfg.url,sizeof cfg.url,"%s/%s",base,path);}
int main(int argc,char **argv) {
    assert(argc==2);const char *base=argv[1];
    handler(EVT_INIT,0,0);
    assert(mode==SETTINGS && mock_net_calls==0 && !mock_timer);
    assert(strstr(visible_text,"Battery 87%"));
    start_dashboard();assert(mode==SETTINGS && settings_error[0]);
    handler(EVT_POINTERUP,40,170);assert(mock_keyboard && keyboard_open);
    char url[1024];snprintf(url,sizeof url,"%s/good.bmp",base);mock_keyboard(url);
    assert(!keyboard_open && !strcmp(cfg.url,url));
    edit_field(1);mock_keyboard("0");assert(cfg.interval_seconds==1200 && settings_error[0]);
    edit_field(1);mock_keyboard("15");assert(cfg.interval_seconds==900);
    edit_field(2);mock_keyboard("22:30");assert(cfg.quiet_start==1350);
    edit_field(3);mock_keyboard("07:15");assert(cfg.quiet_end==435);
    edit_field(3);mock_keyboard("25:10");assert(cfg.quiet_end==435 && settings_error[0]);
    start_dashboard();assert(mode==RUNNING && mock_timer==cycle && !mock_auto_off);
    dashboard_config saved;char error[256];assert(config_load(CFG,&saved,error,sizeof error));
    assert(saved.quiet_start==1350 && saved.quiet_end==435 && saved.interval_seconds==900);
    assert(fetch_image());assert(screen_image && fitted_image && !mock_wifi);
    assert(fitted_image->width==692 && fitted_image->height==936 && image_x==33);
    assert(strstr(visible_text,"Connecting to Wi-Fi") && strstr(visible_text,"Downloading image"));
    assert(strstr(visible_text,"Refreshing the dashboard") && strstr(visible_text,"Finished OK"));
    ibitmap *old=screen_image;time_t last=last_success;
    url_for(base,"missing.bmp");assert(!fetch_image());assert(strstr(status_text,"404"));
    assert(screen_image==old && last_success==last && !mock_wifi);
    url_for(base,"bad.bmp");assert(!fetch_image());assert(screen_image==old && !mock_wifi);
    url_for(base,"truncated.bmp");assert(!fetch_image());assert(screen_image==old && !mock_wifi);
    url_for(base,"oversize.bmp");assert(!fetch_image());assert(strstr(status_text,"2 MiB") && screen_image==old && !mock_wifi);
    url_for(base,"good.bmp");mock_scale_fail=1;assert(!fetch_image());mock_scale_fail=0;
    assert(screen_image==old && last_success==last && strstr(status_text,"scale"));
    mock_load_fail=1;assert(!fetch_image());mock_load_fail=0;
    assert(screen_image==old && strstr(status_text,"8-bit grayscale"));
    mock_net_result=42;assert(!fetch_image());assert(strstr(status_text,"Wi-Fi") && !mock_wifi);mock_net_result=0;
    handler(EVT_POINTERUP,40,1000);assert(mode==SETTINGS && !mock_timer && mock_auto_off);
    mock_battery=-1;mock_charging=1;visible_text[0]=0;draw_settings();
    assert(strstr(visible_text,"Battery --") && strstr(visible_text,"Charging"));
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);int m=local.tm_hour*60+local.tm_min;
    cfg.quiet_start=m;cfg.quiet_end=(m+30)%1440;mode=RUNNING;next_update=0;
    int calls=mock_net_calls;cycle();assert(mock_net_calls==calls && next_update>now && !mock_wifi);
    assert(strstr(status_text,"Quiet hours") && mock_timer==sleep_until_update && mock_timer_delay==NETWORK_SETTLE_MS);
    next_update=mock_clock_time()+900;failed_sleeps=0;
    for(int i=0;i<6;i++)sleep_until_update();
    assert(mode==RUNNING && failed_sleeps==6 && mock_timer==cycle && mock_timer_delay==SLEEP_RETRY_MS);
    mock_sleep_elapsed=3;sleep_until_update();mock_sleep_elapsed=0;
    assert(mode==RUNNING && failed_sleeps==0 && mock_timer_delay==200);
    mock_usb=0;
    for(int i=0;i<MAX_SHORT_SLEEPS;i++)sleep_until_update();
    assert(mode==PAUSED && mock_auto_off && mock_sleep_calls==19 && strstr(status_text,"Paused"));
    assert(!strstr(status_text,"USB"));
    FILE *log=fopen(LOG,"r");assert(log);char line[1024];int found_sleep=0,found_radio=0;
    while(fgets(line,sizeof line,log)) {
        if(strstr(line,"short_count=12") && strstr(line,"usb=0") && strstr(line,"network_flags=0"))found_sleep=1;
        if(strstr(line,"network_off disconnect_result=0"))found_radio=1;
    }
    fclose(log);assert(found_sleep && found_radio);
    handler(EVT_KEYPRESS,IV_KEY_HOME,0);assert(mock_closed);
    handler(EVT_EXIT,0,0);
    puts("Client tests passed (settings, HTTP, invalid images, cache, status, quiet hours, sleep refusal).");return 0;
}
