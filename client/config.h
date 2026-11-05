#ifndef DASHBOARD_CONFIG_H
#define DASHBOARD_CONFIG_H
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {
    char url[1024];
    int interval_seconds, quiet_start, quiet_end;
    char timezone[128];
} dashboard_config;
static void config_defaults(dashboard_config *c) {
    memset(c, 0, sizeof *c);
    c->interval_seconds = 1200; c->quiet_start = 1320; c->quiet_end = 420;
}
static int parse_integer(const char *s, int low, int high, int *value) {
    char *end;
    if (!s || !*s) return 0;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *end || v < low || v > high) return 0;
    *value = (int)v; return 1;
}
static int parse_hhmm(const char *s, int *value) {
    if (!s || strlen(s) != 5 || s[2] != ':' ||
        !isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
        !isdigit((unsigned char)s[3]) || !isdigit((unsigned char)s[4])) return 0;
    int h = (s[0]-'0')*10+s[1]-'0', m = (s[3]-'0')*10+s[4]-'0';
    if (h > 23 || m > 59) return 0;
    *value = h*60+m; return 1;
}
static int config_valid(const dashboard_config *c, char *error, size_t n) {
    const char *host = !strncmp(c->url,"http://",7) ? c->url+7 : !strncmp(c->url,"https://",8) ? c->url+8 : NULL;
    if (!host || !*host || *host=='/' || strpbrk(c->url," \t\r\n")) {
        snprintf(error,n,"Enter a complete http:// or https:// image URL without spaces."); return 0;
    }
    if (c->interval_seconds < 60 || c->interval_seconds > 86400 || c->interval_seconds % 60) {
        snprintf(error,n,"Refresh interval must be 1 to 1440 whole minutes."); return 0;
    }
    if (c->quiet_start < 0 || c->quiet_start > 1439 || c->quiet_end < 0 || c->quiet_end > 1439) {
        snprintf(error,n,"Quiet hours must be between 00:00 and 23:59."); return 0;
    }
    if (strpbrk(c->timezone,"\r\n")) { snprintf(error,n,"Invalid timezone."); return 0; }
    error[0]=0; return 1;
}
static int config_load(const char *path,dashboard_config *c,char *error,size_t n) {
    config_defaults(c);
    FILE *f=fopen(path,"r");
    if (!f) {
        if(errno==ENOENT) {error[0]=0;return 1;}
        snprintf(error,n,"Cannot read settings: %s",strerror(errno));return 0;
    }
    char line[1400]; int ok=1,hour;
    while(fgets(line,sizeof line,f)) {
        if(!strchr(line,'\n')&&!feof(f)) {ok=0;break;}
        line[strcspn(line,"\r\n")]=0;
        if(!line[0]||line[0]=='#')continue;
        char *v=strchr(line,'='); if(!v){ok=0;break;} *v++=0;
        if(!strcmp(line,"url")) {if(strlen(v)>=sizeof c->url){ok=0;break;}strcpy(c->url,v);}
        else if(!strcmp(line,"interval_seconds")) {if(!parse_integer(v,60,86400,&c->interval_seconds)||c->interval_seconds%60){ok=0;break;}}
        else if(!strcmp(line,"quiet_start")) {if(!parse_hhmm(v,&c->quiet_start)){ok=0;break;}}
        else if(!strcmp(line,"quiet_end")) {if(!parse_hhmm(v,&c->quiet_end)){ok=0;break;}}
        else if(!strcmp(line,"quiet_start_hour")||!strcmp(line,"quiet_end_hour")) {
            if(!parse_integer(v,0,23,&hour)){ok=0;break;}
            if(!strcmp(line,"quiet_start_hour"))c->quiet_start=hour*60;else c->quiet_end=hour*60;
        } else if(!strcmp(line,"timezone")) {if(strlen(v)>=sizeof c->timezone){ok=0;break;}strcpy(c->timezone,v);}
    }
    if(ferror(f))ok=0;
    fclose(f);
    snprintf(error,n,"%s",ok?"":"Some saved settings are invalid. Review them before starting.");
    return ok;
}
static int config_save(const char *path,const dashboard_config *c,char *error,size_t n) {
    if(!config_valid(c,error,n))return 0;
    char tmp[1024];
    if(snprintf(tmp,sizeof tmp,"%s.tmp",path)>=(int)sizeof tmp){snprintf(error,n,"Settings path is too long.");return 0;}
    FILE *f=fopen(tmp,"w");
    if(!f){snprintf(error,n,"Cannot save settings: %s",strerror(errno));return 0;}
    int ok=fprintf(f,"url=%s\ninterval_seconds=%d\nquiet_start=%02d:%02d\nquiet_end=%02d:%02d\ntimezone=%s\n",
        c->url,c->interval_seconds,c->quiet_start/60,c->quiet_start%60,c->quiet_end/60,c->quiet_end%60,c->timezone)>0;
    if(fflush(f)||fsync(fileno(f)))ok=0;
    if(fclose(f))ok=0;
    if(ok&&!rename(tmp,path))return 1;
    snprintf(error,n,"Cannot save settings: %s",strerror(errno));unlink(tmp);return 0;
}
#endif
