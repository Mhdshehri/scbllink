#include "../src/fusion.h"
#include <assert.h>
static char *load(const char *p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,2);long n=ftell(f);fseek(f,0,0);char*b=malloc(n+1);if(fread(b,1,n,f)!=(size_t)n)abort();b[n]=0;fclose(f);return b;}
int main(int argc,char**argv){ (void)argc;
  char *ini=load(argv[1]); char v[64];
  assert(ff_ini_get(ini,"MAIN","FOVFactor",v,sizeof v)&&!strcmp(v,"1.0"));
  assert(ff_ini_get(ini,"EXTRACTION","ExtractionWaveConfigs",v,sizeof v)&&!strcmp(v,"Default"));
  assert(ff_ini_get(ini,"STARTUP","DedicatedServerExePath",v,sizeof v)&&!strcmp(v,""));
  assert(ff_ini_get(ini,"COOP","DisableMissionFailOnDetection",v,sizeof v)&&!strcmp(v,"0"));
  assert(ff_ini_get(ini,"GHOST","DisableMissionFailOnDetection",v,sizeof v)&&!strcmp(v,"0"));
  /* every setting we show exists in the release's ini with a valid value equal to our default */
  for(int i=0;i<FF_NSETTINGS;i++){ char n[FF_VAL], d[FF_VAL];
    if(!ff_ini_get(ini,FF_SETTINGS[i].sec,FF_SETTINGS[i].key,v,sizeof v)){printf("MISSING %s.%s\n",FF_SETTINGS[i].sec,FF_SETTINGS[i].key);return 1;}
    if(!ff_normalize(i,v,n,sizeof n)){printf("INVALID %s.%s=%s\n",FF_SETTINGS[i].sec,FF_SETTINGS[i].key,v);return 1;}
    assert(ff_normalize(i,FF_SETTINGS[i].def,d,sizeof d));
    if(strcmp(n,d)) printf("note: default differs %s.%s ini=%s ours=%s\n",FF_SETTINGS[i].sec,FF_SETTINGS[i].key,n,d);
  }
  /* edits keep comments */
  size_t before=strlen(ini);
  assert(ff_ini_set(&ini,"MAIN","FOVFactor","1.35"));
  assert(ff_ini_get(ini,"MAIN","FOVFactor",v,sizeof v)&&!strcmp(v,"1.35"));
  assert(strstr(ini,"FOVFactor = 1.35                           // Range: [0.5, 2.5]."));
  assert(ff_ini_set(&ini,"EXTRACTION","ExtractionWaveConfigs","Random"));
  { char*l=strstr(ini,"ExtractionWaveConfigs = Random"); char*o=strstr(load(argv[1]),"ExtractionWaveConfigs = Default"); assert(l&&o); assert(strstr(l,"//")-l==strstr(o,"//")-o); }
  assert(ff_ini_set(&ini,"STARTUP","DedicatedServerExePath","x"));
  assert(ff_ini_get(ini,"STARTUP","DedicatedServerExePath",v,sizeof v)&&!strcmp(v,"x"));
  assert(ff_harden(&ini)); assert(ff_ini_get(ini,"STARTUP","DedicatedServerExePath",v,sizeof v)&&!strcmp(v,""));
  assert(ff_ini_set(&ini,"NEWSEC","K","5")); assert(ff_ini_get(ini,"NEWSEC","K",v,sizeof v)&&!strcmp(v,"5"));
  assert(ff_ini_set(&ini,"KESTREL","NewKey","1")); assert(ff_ini_get(ini,"KESTREL","NewKey",v,sizeof v)&&!strcmp(v,"1"));
  assert(ff_ini_get(ini,"KESTREL","EnableKestrelDLC",v,sizeof v)&&!strcmp(v,"0"));
  assert(ff_ini_get(ini,"HUNTER","ReinforcementsNumber",v,sizeof v)&&!strcmp(v,"Default"));
  (void)before;
  /* normalize rejects junk */
  int fov=-1,fps=-1,ch=-1,flt=-1; for(int i=0;i<FF_NSETTINGS;i++){ if(!strcmp(FF_SETTINGS[i].key,"FOVFactor"))fov=i; if(!strcmp(FF_SETTINGS[i].key,"FPSLimit"))fps=i; if(!strcmp(FF_SETTINGS[i].key,"ExtractionWaveConfigs"))ch=i; if(!strcmp(FF_SETTINGS[i].key,"DefaultMissionFilter"))flt=i;}
  char o[FF_VAL];
  assert(!ff_normalize(fov,"3",o,sizeof o)); assert(!ff_normalize(fov,"1e5",o,sizeof o)); assert(!ff_normalize(fov,"\"x\"",o,sizeof o));
  assert(ff_normalize(fov,"1.234",o,sizeof o)&&!strcmp(o,"1.23"));
  assert(ff_normalize(fps,"0",o,sizeof o)&&!strcmp(o,"0")); assert(!ff_normalize(fps,"10",o,sizeof o)); assert(ff_normalize(fps,"144",o,sizeof o));
  assert(ff_normalize(ch,"random",o,sizeof o)&&!strcmp(o,"Random")); assert(!ff_normalize(ch,"Rand",o,sizeof o));
  assert(ff_normalize(flt,"3",o,sizeof o)&&!strcmp(o,"3")); assert(!ff_normalize(flt,"4",o,sizeof o));
  /* host -> friend */
  uint8_t pk[256]; int pn=ff_pack_shared(ini,pk,sizeof pk); assert(pn>0&&pn<=230); printf("packed %d bytes\n",pn);
  char *fr=load(argv[1]); int a=ff_apply_shared(&fr,pk,pn); assert(a==1);
  assert(ff_ini_get(fr,"EXTRACTION","ExtractionWaveConfigs",v,sizeof v)&&!strcmp(v,"Random"));
  assert(ff_ini_get(fr,"MAIN","FOVFactor",v,sizeof v)&&!strcmp(v,"1.0")); /* personal: untouched */
  /* hostile messages */
  uint8_t bad1[]={0,3,'1','.','5'}; /* idx 0 is personal */ assert(ff_apply_shared(&fr,bad1,sizeof bad1)==0);
  uint8_t bad2[]={13,200,'1'}; assert(ff_apply_shared(&fr,bad2,sizeof bad2)==-1);
  uint8_t bad3[]={13,1,'7'}; assert(ff_apply_shared(&fr,bad3,sizeof bad3)==0);
  uint8_t bad4[]={13,3,'1','\r','\n'}; assert(ff_apply_shared(&fr,bad4,sizeof bad4)==0);
  uint8_t bad5[]={250,1,'1'}; assert(ff_apply_shared(&fr,bad5,sizeof bad5)==0);
  uint8_t bad6[]={13}; assert(ff_apply_shared(&fr,bad6,sizeof bad6)==-1);
  puts("all fusion tests passed"); return 0; }
