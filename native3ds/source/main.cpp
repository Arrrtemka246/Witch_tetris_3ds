#include <3ds.h>
#include <citro2d.h>
#include <mpg123.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <string>
#include <vector>
#include <sys/stat.h>

static constexpr int TOP_W=400, H=240;
static constexpr int BW=10, BH=20;
// The two LCDs do not touch physically.  Treat the hinge as a hidden strip of
// the same virtual scene so the eye does not stretch the throne across it.
static constexpr float DUAL_HINGE_GAP=56.0f;
static constexpr u32 BG=C2D_Color32(8,5,18,255);
static constexpr u32 WHITE=C2D_Color32(245,240,255,255);
static constexpr u32 ACCENT=C2D_Color32(218,167,255,255);
static constexpr u32 DIM=C2D_Color32(150,140,170,255);
static constexpr u32 RED=C2D_Color32(255,90,110,255);
static constexpr u32 GREEN=C2D_Color32(70,255,125,255);

static C3D_RenderTarget *topTarget=nullptr,*topLeftTarget=nullptr,*topRightTarget=nullptr,*botTarget=nullptr;
static C2D_TextBuf textBuf=nullptr;
static C2D_Font sysFont=nullptr;
static float eyeShift=0.0f;
static float stereoSlider=0.0f;
static float textDepth=0.38f;
static float textZ=0.8f;
static bool drawingBottom=false;
static float stereoX(float z){ return drawingBottom?0.0f:eyeShift*std::max(0.0f,(z-0.04f)*7.5f); }
static void beginTop(){drawingBottom=false;C2D_SceneBegin(topTarget);}
static void beginBottom(){drawingBottom=true;C2D_SceneBegin(botTarget);}

static void drawText(float x,float y,float scale,u32 color,const char* fmt,...){
    char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    C2D_Text t; C2D_TextBufClear(textBuf); C2D_TextFontParse(&t,sysFont,textBuf,buf); C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_WithColor,x+stereoX(textDepth),y,textZ,scale,scale,color);
}
static void centerText(float cx,float y,float scale,u32 color,const char* s){
    C2D_Text t; C2D_TextBufClear(textBuf); C2D_TextFontParse(&t,sysFont,textBuf,s); C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_WithColor,cx-t.width*scale*0.5f+stereoX(textDepth),y,textZ,scale,scale,color);
}

struct Art {
    C2D_SpriteSheet sheet=nullptr;
    bool load(const char* path){ sheet=C2D_SpriteSheetLoad(path); return sheet!=nullptr; }
    void free(){ if(sheet){ C2D_SpriteSheetFree(sheet); sheet=nullptr; } }
    void drawFit(float x,float y,float w,float h,float z=0.1f,float alpha=1.0f) const {
        if(!sheet) return; C2D_Image im=C2D_SpriteSheetGetImage(sheet,0);
        float iw=(float)im.subtex->width, ih=(float)im.subtex->height;
        float s=std::min(w/iw,h/ih); float dx=x+(w-iw*s)/2+stereoX(z), dy=y+(h-ih*s)/2;
        // A white tint with blend factor 1.0 replaces the source RGB and turns
        // every character into a white silhouette.  Most art is fully opaque,
        // so draw it without a tint and preserve the original palette.
        (void)alpha;
        C2D_DrawImageAt(im,dx,dy,z,nullptr,s,s);
    }
    void drawCover(float x,float y,float w,float h,float z=0.1f) const {
        if(!sheet) return; C2D_Image im=C2D_SpriteSheetGetImage(sheet,0);
        float iw=(float)im.subtex->width, ih=(float)im.subtex->height;
        float s=std::max(w/iw,h/ih); float dx=x+(w-iw*s)/2+stereoX(z), dy=y+(h-ih*s)/2;
        C2D_DrawImageAt(im,dx,dy,z,nullptr,s,s);
    }
    void drawStretch(float x,float y,float w,float h,float z=0.1f) const {
        if(!sheet) return; C2D_Image im=C2D_SpriteSheetGetImage(sheet,0);
        float sx=w/(float)im.subtex->width,sy=h/(float)im.subtex->height;
        C2D_DrawImageAt(im,x+stereoX(z),y,z,nullptr,sx,sy);
    }
    void drawDualContinuation(bool bottom,float z=0.1f) const {
        if(!sheet) return;C2D_Image im=C2D_SpriteSheetGetImage(sheet,0);
        float iw=(float)im.subtex->width,ih=(float)im.subtex->height;
        float virtualH=H*2.0f+DUAL_HINGE_GAP;
        // Uniform scale keeps the castle architecture undistorted.  The lower
        // LCD is the centred 320-pixel viewport of the same 400-pixel scene.
        float s=std::max(TOP_W/iw,virtualH/ih);
        float sceneX=(TOP_W-iw*s)*0.5f;
        float viewX=bottom?40.0f:0.0f;
        float viewY=bottom?(H+DUAL_HINGE_GAP):0.0f;
        C2D_DrawImageAt(im,sceneX-viewX+stereoX(z),-viewY,z,nullptr,s,s);
    }
};

struct Sheet {
    C2D_SpriteSheet sheet=nullptr;
    bool load(const char* path){ sheet=C2D_SpriteSheetLoad(path); return sheet!=nullptr; }
    void free(){ if(sheet){ C2D_SpriteSheetFree(sheet); sheet=nullptr; } }
    void draw(int index,float x,float y,float size,float z=0.5f) const {
        if(!sheet || index<0) return;
        C2D_Image im=C2D_SpriteSheetGetImage(sheet,index);
        float sx=size/(float)im.subtex->width;
        float sy=size/(float)im.subtex->height;
        C2D_DrawImageAt(im,x+stereoX(z),y,z,nullptr,sx,sy);
    }
};

struct Audio {
    static constexpr int BUF_SAMPLES=4096, BUF_SIZE=BUF_SAMPLES*2*2;
    int channel=0;
    mpg123_handle* mh=nullptr; ndspWaveBuf wb[2]{}; u8* mem=nullptr;
    bool ready=false, playing=false, paused=false, finished=false, looping=false;
    long rate=44100; float volume=1.0f; std::string path;
    explicit Audio(int ch=0):channel(ch){}
    bool init(bool systemOwner=false){
        if(systemOwner && R_FAILED(ndspInit())) return false;
        if(systemOwner && mpg123_init()!=MPG123_OK){ ndspExit(); return false; }
        mem=(u8*)linearAlloc(BUF_SIZE*2); ready=mem!=nullptr; return ready;
    }
    void stop(){
        if(!ready) return; ndspChnReset(channel); if(mh){ mpg123_close(mh); mpg123_delete(mh); mh=nullptr; }
        playing=paused=finished=false;
    }
    size_t fill(u8* b){
        size_t total=0;
        while(total<BUF_SIZE){ size_t done=0; int r=mpg123_read(mh,b+total,BUF_SIZE-total,&done); total+=done; if(r==MPG123_DONE||r==MPG123_ERR||done==0) break; }
        return total;
    }
    bool playFrom(const char* p,off_t sample,bool loop=false){
        if(!ready) return false; stop(); int err=0; mh=mpg123_new(nullptr,&err); if(!mh) return false;
        mpg123_param(mh,MPG123_ADD_FLAGS,MPG123_FORCE_STEREO,0);
        if(mpg123_open(mh,p)!=MPG123_OK){ mpg123_delete(mh); mh=nullptr; return false; }
        int ch=0,enc=0; if(mpg123_getformat(mh,&rate,&ch,&enc)!=MPG123_OK){ stop(); return false; }
        mpg123_format_none(mh); mpg123_format(mh,rate,MPG123_STEREO,MPG123_ENC_SIGNED_16);
        if(sample>0)mpg123_seek(mh,sample,SEEK_SET);
        ndspChnReset(channel); ndspChnSetInterp(channel,NDSP_INTERP_LINEAR); ndspChnSetRate(channel,(float)rate); ndspChnSetFormat(channel,NDSP_FORMAT_STEREO_PCM16);
        float mix[12]={volume,volume}; ndspChnSetMix(channel,mix);
        memset(wb,0,sizeof(wb));
        for(int i=0;i<2;i++){ u8* b=mem+i*BUF_SIZE; size_t n=fill(b); if(!n) break; DSP_FlushDataCache(b,BUF_SIZE); wb[i].data_vaddr=b; wb[i].nsamples=n/4; ndspChnWaveBufAdd(channel,&wb[i]); }
        path=p; looping=loop; playing=true; paused=false; finished=false; return true;
    }
    bool play(const char* p,bool loop=false){ return playFrom(p,0,loop); }
    off_t tell() const { return mh?mpg123_tell(mh):0; }
    void setVolume(float v){ volume=std::max(0.0f,std::min(1.0f,v));float mix[12]={volume,volume};if(ready)ndspChnSetMix(channel,mix); }
    void setPause(bool p){ if(!playing) return; paused=p; ndspChnSetPaused(channel,p); }
    void update(){
        if(!playing||paused||!mh) return;
        for(int i=0;i<2;i++) if(wb[i].status==NDSP_WBUF_DONE){ u8* b=mem+i*BUF_SIZE; size_t n=fill(b); if(!n){
            if(looping){ std::string cp=path; play(cp.c_str(),true); } else { stop(); finished=true; } return;
        } DSP_FlushDataCache(b,BUF_SIZE); wb[i].nsamples=n/4; wb[i].status=NDSP_WBUF_FREE; ndspChnWaveBufAdd(channel,&wb[i]); }
    }
    void fini(bool systemOwner=false){ stop(); if(mem) linearFree(mem); mem=nullptr; ready=false;if(systemOwner){ mpg123_exit(); ndspExit(); } }
};
static Audio audio(0),voiceAudio(1),sfxAudio(2);

struct Music {
    std::vector<std::string> pool, bag; std::string fixedFirst,last; bool firstPending=true, firstCycle=true; int phase=-1;
    bool autoAdvance=true;
    void configure(int p){
        if(phase==p) return; phase=p; pool.clear(); bag.clear(); firstPending=true; firstCycle=true; last.clear();
        if(p==0){ fixedFirst="romfs:/audio/menu_1.mp3"; pool={"romfs:/audio/menu_1.mp3","romfs:/audio/menu_2.mp3"}; }
        // These are the exact user_music phase folders used by main.py.  The
        // temporary arcade/bonus additions are deliberately absent.
        else if(p==1){ fixedFirst="romfs:/audio/music_p0_opening.mp3"; pool={
            "romfs:/audio/music_p0_00.mp3","romfs:/audio/music_p0_01.mp3","romfs:/audio/music_p0_02.mp3",
            "romfs:/audio/music_p0_03.mp3","romfs:/audio/music_p0_04.mp3","romfs:/audio/music_p0_05.mp3",
            "romfs:/audio/music_p0_06.mp3","romfs:/audio/music_p0_07.mp3","romfs:/audio/music_p0_08.mp3",
            "romfs:/audio/music_p0_09.mp3","romfs:/audio/music_p0_10.mp3"}; }
        else if(p==2){ fixedFirst.clear(); pool={
            "romfs:/audio/music_p1_00.mp3","romfs:/audio/music_p1_01.mp3","romfs:/audio/music_p1_02.mp3",
            "romfs:/audio/music_p1_03.mp3","romfs:/audio/music_p1_04.mp3","romfs:/audio/music_p1_05.mp3",
            "romfs:/audio/music_p1_06.mp3","romfs:/audio/music_p1_07.mp3","romfs:/audio/music_p1_08.mp3",
            "romfs:/audio/music_p1_09.mp3","romfs:/audio/music_p1_10.mp3","romfs:/audio/music_p1_11.mp3",
            "romfs:/audio/music_p1_12.mp3","romfs:/audio/music_p1_13.mp3"}; }
        else if(p==3){ fixedFirst.clear(); pool={
            "romfs:/audio/music_guardians_00.mp3","romfs:/audio/music_guardians_01.mp3",
            "romfs:/audio/music_guardians_02.mp3","romfs:/audio/music_guardians_03.mp3",
            "romfs:/audio/music_guardians_04.mp3"}; }
        else { fixedFirst="romfs:/audio/music_phobos_opening.mp3"; pool={
            "romfs:/audio/music_phobos_00.mp3","romfs:/audio/music_phobos_01.mp3",
            "romfs:/audio/music_phobos_02.mp3","romfs:/audio/music_phobos_03.mp3",
            "romfs:/audio/music_phobos_04.mp3"}; }
    }
    void refillBag(){
        bag.clear();
        for(const auto& p: pool){
            if(firstCycle && p==fixedFirst) continue;
            bag.push_back(p);
        }
        if(bag.empty()) bag=pool;
        for(int i=(int)bag.size()-1;i>0;i--){ int j=rand()%(i+1); std::swap(bag[i],bag[j]); }
        if(bag.size()>1 && bag.back()==last){
            for(size_t i=0;i<bag.size()-1;i++){
                if(bag[i]!=last){ std::swap(bag[i],bag.back()); break; }
            }
        }
        firstCycle=false;
    }
    std::string nextPath(){
        if(firstPending){ firstPending=false; if(!fixedFirst.empty()){last=fixedFirst;return last;} }
        if(bag.empty()) refillBag();
        std::string p=bag.back(); bag.pop_back(); last=p; return p;
    }
    void playNext(){ std::string p=nextPath(); audio.play(p.c_str()); }
    void start(int p){
        if(phase==p){ bag.clear(); firstPending=true; firstCycle=true; last.clear(); }
        else configure(p);
        playNext();
    }
    void update(){
        audio.update();
        if(audio.finished && autoAdvance){ audio.finished=false; playNext(); }
    }
} music;

enum Mode { BOOT, MENU, RECORDS, GALLERY, SETTINGS, GAME, CUTSCENE, WINNER,
            ROUTE_VICTORY, ENDING, ROOM_ENTRY, PHOBOS_ROOM, VIDEO_MODE,
            VTD_MODE, PORN_GALLERY, JETIX_MODE, CARD_MODE };
static Mode mode=BOOT, returnMode=MENU, cutsceneReturn=MENU;
static bool running=true, paused=false, gameOver=false, dualScreen=false, phobosFall=true;
static bool phobosDeletedWin=false,emptyRosterBored=false,clearPending=false;
static bool phobosRoute=false, guardiansRoute=false, horrorPieces=false;
static bool pieceEnabled[7]={true,true,true,true,true,true,true},phobosEnabled=true,settingsRoster=false;
static int startSpeed=1;
static int menuIndex=0,recordsIndex=0,galleryIndex=0,settingsIndex=0,pauseIndex=0,gameOverIndex=0,winnerChoice=0;
static bool recordsConfirmReset=false;
static int cutsceneStage=0,cutscenePage=0,roomPose=0,roomLine=0,pornImage=0,endingPage=0;
static int story100Tick=0,story100Variant=0;
static bool story100AudioStopped=false;
static int score=0,lines=0,level=1,frameCounter=0,secretTimer=0;
static int gravityFrames=0,lockFrames=0,lockResets=0,dasDirection=0,dasFrames=0;
static int rotationCount=0,rotationHintBlockPieces=0,holdCountWindow=0,emptyRosterFrames=0,boredFrames=0;
static int voiceCooldown=0,playFrames=0,pauseHintEligibleAt=0,clearBeforeLines=0;
static int consecutiveGameOvers=0;
static bool pauseHintPlayed=false,gameOverVoiceHandled=false,loserStreakVoiceUsed=false,layoutReactionDone=false;
static bool spawnVoiceUsed[8]{};
static int victoryTimer=0,roomEntryTimer=0;
static int clearFxTimer=0,clearFxCount=0,gameplayMatrixTimer=0,gameplayJetixTimer=0,gameplayVtdTimer=0;
static int clearFxRows[4]={};
static int board[BH][BW]{};
static int curType=0,curRot=0,curX=3,curY=-1,nextType=1,holdType=-1;
static bool holdUsed=false;
static int bag[7],bagPos=7,bagCount=7;
static int spawnHistory[8]{},spawnHistoryCount=0,pieceSerial=0,lastSeenPiece[7]{};
static bool recordSaved=false;
static std::string codeBuffer;
static bool keyboardRussian=false;
static int story200ArtemCount=0;
static bool winnerMusicSaved=false,musicDucked=false;
static std::string winnerMusicPath;
static off_t winnerMusicSample=0;
static bool winnerMusicLooping=false;

static Art bgMenu,bgGame[3],bgDual[3],phobosMenu,phobosGame,vtdObs,roomBg,roomFg,roomPoses[6];
static Art introCastle,introHall,introPhobos,introNormal[7],introFinal[7],l100Will,l100Phobos,l100Heart,terminalHeartMask,terminalJetixMask,endingHeart,endingWitch,endingArt[7];
static Art pornArts[2],jetixLogo,chatgptLogo,sunoLogo,videoFrame,endingFrame;
static Sheet phaseCells,horrorCells;
static int videoKind=0,videoTick=0,videoLoaded=-1,videoCount=0,videoFps=0;
static int endingTick=0,endingLoaded=-1;
static u64 endingStartMs=0;
static bool endingFinished=false;
static constexpr int ENDING_FRAME_COUNT=430,ENDING_FPS=15;

static const char* charNames[7]={"CORNELIA","BLUNK","CALEB","IRMA","WILL","TARANEE","HAY LIN"};
static const char* charFiles[7]={"cornelia","blunk","caleb","irma","will","taranee","haylin"};
static const char* roomLines[]={
 "Отсюда выхода нет.","Ты сам выбрал этот путь.","Можешь нажимать кнопки сколько угодно.",
 "Я всё ещё здесь.","Не торопись. Комната никуда не денется.","Снова проверяешь кнопки?",
 "Твои правила здесь больше не действуют.","Мы ещё не закончили.","Сколько раз ты попробуешь уйти?",
 "Останься. Посмотрим, кто устанет первым.","Ты ведь понимаешь: меню уже не поможет.",
 "Закрыть программу можешь. Двери здесь всё равно нет.","Я слышу каждое нажатие.",
 "Неужели ты рассчитывал на кнопку BACK?","Меридиан снаружи. А ты здесь.",
 "Попробуй ещё раз. Мне даже интересно.","Комната запомнила тебя.","Тишина тоже бывает ответом.",
 "Ты хотел увидеть, что будет дальше. Вот оно.","Не волнуйся. Я никуда не спешу."
};
static constexpr int ROOM_LINE_COUNT=sizeof(roomLines)/sizeof(roomLines[0]);

static void ensureSaveDir(){mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/WitchTetris",0777);}
static void saveSettings(){ensureSaveDir();FILE* f=fopen("sdmc:/3ds/WitchTetris/settings.cfg","w");if(f){fprintf(f,"dual=%d\nphobosfall=%d\nphobos=%d\nstartspeed=%d\n",dualScreen?1:0,phobosFall?1:0,phobosEnabled?1:0,startSpeed);for(int i=0;i<7;i++)fprintf(f,"piece%d=%d\n",i,pieceEnabled[i]?1:0);fclose(f);} }
static void loadSettings(){FILE* f=fopen("sdmc:/3ds/WitchTetris/settings.cfg","r");if(!f)return;char k[64];int v;while(fscanf(f,"%63[^=]=%d\n",k,&v)==2){if(!strcmp(k,"dual"))dualScreen=v;if(!strcmp(k,"phobosfall"))phobosFall=v;if(!strcmp(k,"phobos"))phobosEnabled=v;if(!strcmp(k,"startspeed"))startSpeed=std::max(1,std::min(5,v));for(int i=0;i<7;i++){char p[16];snprintf(p,sizeof(p),"piece%d",i);if(!strcmp(k,p))pieceEnabled[i]=v;}}fclose(f);}

struct RecordEntry {int lines,score;RecordEntry(int l=0,int s=0):lines(l),score(s){}};
static std::vector<RecordEntry> records;
static void loadRecords(){records.clear();FILE* f=fopen("sdmc:/3ds/WitchTetris/records.cfg","r");if(!f)return;RecordEntry r;while(fscanf(f,"%d %d",&r.lines,&r.score)==2)records.push_back(r);fclose(f);std::sort(records.begin(),records.end(),[](const RecordEntry&a,const RecordEntry&b){return a.lines!=b.lines?a.lines>b.lines:a.score>b.score;});if(records.size()>10)records.resize(10);}
static void writeRecords(){ensureSaveDir();FILE* f=fopen("sdmc:/3ds/WitchTetris/records.cfg","w");if(!f)return;for(const auto&r:records)fprintf(f,"%d %d\n",r.lines,r.score);fclose(f);}
static void saveRecord(){if(recordSaved)return;recordSaved=true;records.push_back({lines,score});std::sort(records.begin(),records.end(),[](const RecordEntry&a,const RecordEntry&b){return a.lines!=b.lines?a.lines>b.lines:a.score>b.score;});if(records.size()>10)records.resize(10);writeRecords();}
static void resetRecords(){records.clear();writeRecords();recordsConfirmReset=false;recordsIndex=1;}

// Exact geometry from the Python original. Every clockwise rotation is
// normalised to the top-left corner of its own bounding box; rotating in a
// fixed 4x4 box makes the logical cells drift away from their sprite slices.
static const int baseShape[7][4][2]={
 {{0,0},{0,1},{0,2},{0,3}},{{0,0},{1,0},{0,1},{1,1}},{{0,0},{1,0},{2,0},{1,1}},
 {{0,0},{1,0},{1,1},{2,1}},{{1,0},{2,0},{0,1},{1,1}},{{0,0},{0,1},{1,1},{2,1}},{{2,0},{0,1},{1,1},{2,1}}
};
static int shapePos[7][4][4][2];
static bool shapesReady=false;
static void initShapes(){
 if(shapesReady)return;
 for(int t=0;t<7;t++){
  for(int i=0;i<4;i++){shapePos[t][0][i][0]=baseShape[t][i][0];shapePos[t][0][i][1]=baseShape[t][i][1];}
  for(int r=1;r<4;r++){
   int maxY=0,minX=99,minY=99;
   for(int i=0;i<4;i++)maxY=std::max(maxY,shapePos[t][r-1][i][1]);
   int h=maxY+1;
   for(int i=0;i<4;i++){int px=shapePos[t][r-1][i][0],py=shapePos[t][r-1][i][1];shapePos[t][r][i][0]=h-1-py;shapePos[t][r][i][1]=px;minX=std::min(minX,shapePos[t][r][i][0]);minY=std::min(minY,shapePos[t][r][i][1]);}
   for(int i=0;i<4;i++){shapePos[t][r][i][0]-=minX;shapePos[t][r][i][1]-=minY;}
  }
 }
 shapesReady=true;
}
static void blockPos(int t,int r,int i,int& x,int& y){initShapes();x=shapePos[t][r&3][i][0];y=shapePos[t][r&3][i][1];}
static bool fits(int t,int r,int px,int py){if(t<0||t>=7)return false;for(int i=0;i<4;i++){int x,y;blockPos(t,r,i,x,y);x+=px;y+=py;if(x<0||x>=BW||y>=BH)return false;if(y>=0&&board[y][x])return false;}return true;}
static void resetRandomizer(){bagPos=bagCount=7;spawnHistoryCount=pieceSerial=0;memset(lastSeenPiece,0,sizeof(lastSeenPiece));}
static int nextBag(){
 if(bagPos>=bagCount){bagCount=0;for(int i=0;i<7;i++)if(pieceEnabled[i])bag[bagCount++]=i;if(!bagCount)return -1;for(int i=bagCount-1;i>0;i--){int j=rand()%(i+1);std::swap(bag[i],bag[j]);}bagPos=0;}
 return bag[bagPos++];
}
static int nextPhobosPiece(){
 int candidates[7],n=0;for(int i=0;i<7;i++)if(pieceEnabled[i])candidates[n++]=i;if(!n)return -1;
 if(spawnHistoryCount>=3&&spawnHistory[spawnHistoryCount-1]==spawnHistory[spawnHistoryCount-2]&&spawnHistory[spawnHistoryCount-2]==spawnHistory[spawnHistoryCount-3]&&n>1){int repeated=spawnHistory[spawnHistoryCount-1];int out=0;for(int i=0;i<n;i++)if(candidates[i]!=repeated)candidates[out++]=candidates[i];n=out;}
 double weights[7]{},total=0.0;for(int i=0;i<n;i++){int t=candidates[i];int drought=std::max(0,pieceSerial-lastSeenPiece[t]);double w=1.0+std::min(drought,14)*0.075;if(spawnHistoryCount&&spawnHistory[spawnHistoryCount-1]==t){w*=0.62;if(spawnHistoryCount>=2&&spawnHistory[spawnHistoryCount-2]==t)w*=0.22;}weights[i]=w;total+=w;}
 double pick=((double)rand()/(double)RAND_MAX)*total;for(int i=0;i<n;i++){pick-=weights[i];if(pick<=0.0)return candidates[i];}return candidates[n-1];
}
static int nextPiece(){return phobosFall?nextPhobosPiece():nextBag();}
static void rememberSpawn(int t){if(t<0)return;if(spawnHistoryCount<8)spawnHistory[spawnHistoryCount++]=t;else{memmove(spawnHistory,spawnHistory+1,sizeof(int)*7);spawnHistory[7]=t;}pieceSerial++;lastSeenPiece[t]=pieceSerial;}

// The atlases were cut by iterating Python's SHAPES[kind][rotation] lists.
// With the same geometry and cell order there is no per-piece remapping.
static int spriteIndex(int t,int r,int i){return t*16+(r&3)*4+i;}
static int encodeCell(int t,int r,int i,bool horror){return ((t+1)<<8)|(spriteIndex(t,r,i)+1)|(horror?0x10000:0);}
static int cellType(int code){return code<0?(-code-1):(((code>>8)&255)-1);}
static u32 pieceColor(int t){static u32 c[7]={C2D_Color32(80,220,255,255),C2D_Color32(255,220,70,255),C2D_Color32(190,90,255,255),C2D_Color32(80,240,130,255),C2D_Color32(255,80,100,255),C2D_Color32(80,110,255,255),C2D_Color32(255,145,60,255)};return c[(t<0?0:t)%7];}
static bool plainMode(){return guardiansRoute||(phobosRoute&&!horrorPieces);}

static void startCutscene(int stage,Mode after){mode=CUTSCENE;cutsceneStage=stage;cutscenePage=0;cutsceneReturn=after;music.autoAdvance=false;if(stage==0)audio.play("romfs:/audio/intro.mp3");else if(stage==100){story100Tick=0;story100Variant=rand()%8;story100AudioStopped=false;}else audio.play("romfs:/audio/music_cutscene_lines200.mp3");}
static void startEnding(Mode after){if(after!=GALLERY)saveRecord();mode=ENDING;returnMode=after;endingTick=0;endingLoaded=-1;endingFinished=false;endingFrame.free();music.autoAdvance=false;audio.play("romfs:/video/ending.mp3");endingStartMs=osGetTime();}
static void finishRoomEntry(){mode=PHOBOS_ROOM;roomPose=rand()%6;roomLine=rand()%ROOM_LINE_COUNT;audio.play("romfs:/audio/phobos_room.mp3",true);}
static void enterRoom(){if(mode==ROOM_ENTRY||mode==PHOBOS_ROOM)return;saveRecord();mode=ROOM_ENTRY;roomEntryTimer=0;music.autoAdvance=false;audio.stop();voiceAudio.stop();voiceAudio.play("romfs:/audio/phobos_reverse.mp3");}
static void resetClassicLock(){lockFrames=lockResets=0;gravityFrames=0;dasDirection=dasFrames=0;}
static bool playPhobosVoice(const char* path,bool force=false,bool ignoreCooldown=false){
 if(!phobosEnabled||guardiansRoute||gameplayVtdTimer>0||voiceAudio.playing)return false;
 if(!force&&!ignoreCooldown&&voiceCooldown>0)return false;
 if(phobosRoute){char laugh[80];snprintf(laugh,sizeof(laugh),"romfs:/audio/react_phobos_tetris_%d.mp3",1+rand()%3);path=laugh;}
 if(!voiceAudio.play(path))return false;voiceCooldown=60*18;return true;
}
static void maybeSpawnVoice(int t){
 // Exact rare, once-per-run spawn reactions from the Python build.
 if(t==2){
  if(!spawnVoiceUsed[0]&&rand()%1000<45&&playPhobosVoice("romfs:/audio/react_phobos_name_traitors.mp3")){spawnVoiceUsed[0]=true;return;}
  if(!spawnVoiceUsed[1]&&rand()%1000<25&&playPhobosVoice("romfs:/audio/react_phobos_caleb_rebel.mp3"))spawnVoiceUsed[1]=true;
 }else if(t==1){
  int options[2],n=0;if(!spawnVoiceUsed[2])options[n++]=2;if(!spawnVoiceUsed[3])options[n++]=3;
  if(n&&rand()%1000<35){int k=options[rand()%n];if(playPhobosVoice(k==2?"romfs:/audio/react_phobos_blunk_angry.mp3":"romfs:/audio/react_phobos_blunk_annoyed.mp3"))spawnVoiceUsed[k]=true;}
 }else if(t==4){
  if(!spawnVoiceUsed[4]&&rand()%1000<45&&playPhobosVoice("romfs:/audio/react_phobos_need_crystal.mp3")){spawnVoiceUsed[4]=true;return;}
  if(!spawnVoiceUsed[5]&&rand()%1000<45&&playPhobosVoice("romfs:/audio/react_phobos_crystal.mp3")){spawnVoiceUsed[5]=true;return;}
  if(!spawnVoiceUsed[6]&&rand()%1000<18&&playPhobosVoice("romfs:/audio/react_phobos_well_girls.mp3"))spawnVoiceUsed[6]=true;
 }else if(t==0||t==3||t==5||t==6){
  if(!spawnVoiceUsed[6]&&rand()%1000<18&&playPhobosVoice("romfs:/audio/react_phobos_well_girls.mp3")){spawnVoiceUsed[6]=true;return;}
  if(!spawnVoiceUsed[7]&&rand()%1000<8&&playPhobosVoice("romfs:/audio/react_phobos_guardian.mp3"))spawnVoiceUsed[7]=true;
 }
}
static void handleGameOverVoice(){
 if(gameOverVoiceHandled||emptyRosterBored||lines>=200)return;gameOverVoiceHandled=true;consecutiveGameOvers++;
 if(consecutiveGameOvers>=5&&!loserStreakVoiceUsed&&playPhobosVoice("romfs:/audio/react_phobos_you_loser.mp3",true)){loserStreakVoiceUsed=true;return;}
 if(rand()%100<38)playPhobosVoice("romfs:/audio/react_phobos_expected_no_less.mp3",true);
}
static void spawn(){curType=nextType;rotationCount=0;if(rotationHintBlockPieces>0)rotationHintBlockPieces--;if(curType<0){nextType=-1;curRot=0;curX=3;curY=-1;holdUsed=false;resetClassicLock();return;}rememberSpawn(curType);nextType=nextPiece();curRot=0;curX=3;curY=-1;holdUsed=false;resetClassicLock();maybeSpawnVoice(curType);if(!fits(curType,curRot,curX,curY)){if(phobosRoute)enterRoom();else if(guardiansRoute)startEnding(MENU);else{gameOver=true;gameOverIndex=0;saveRecord();handleGameOverVoice();}}}
static void newGame(){memset(board,0,sizeof(board));score=lines=0;level=1;gameOver=paused=false;recordSaved=false;story200ArtemCount=0;winnerMusicSaved=false;holdType=-1;holdUsed=false;resetRandomizer();resetClassicLock();phobosRoute=guardiansRoute=horrorPieces=false;clearFxTimer=gameplayMatrixTimer=gameplayJetixTimer=gameplayVtdTimer=0;clearPending=false;rotationCount=rotationHintBlockPieces=holdCountWindow=emptyRosterFrames=boredFrames=0;voiceCooldown=playFrames=0;pauseHintEligibleAt=60*(150+rand()%271);pauseHintPlayed=gameOverVoiceHandled=layoutReactionDone=false;memset(spawnVoiceUsed,0,sizeof(spawnVoiceUsed));emptyRosterBored=false;phobosDeletedWin=!phobosEnabled;mode=GAME;
 if(phobosDeletedWin){curType=nextType=-1;music.autoAdvance=false;audio.stop();voiceAudio.stop();return;}
 nextType=nextPiece();spawn();music.autoAdvance=true;music.start(1);if(rand()%100<65){int roll=rand()%100,idx=roll<25?0:roll<50?1:roll<74?2:roll<83?3:roll<91?4:5;char p[80];snprintf(p,sizeof(p),"romfs:/audio/react_phobos_start_%d.mp3",idx);voiceAudio.stop();if(voiceAudio.play(p))voiceCooldown=60*18;}}

static void triggerMilestones(int before){
 if(before<100&&lines>=100){startCutscene(100,GAME);return;}
 if(before<200&&lines>=200){startCutscene(200,WINNER);return;}
 if(phobosRoute&&before<300&&lines>=300){enterRoom();return;}
 int phase=lines<100?1:lines<200?2:guardiansRoute?3:4;
 if(music.phase!=phase)music.start(phase);
}
static void developerAddLines(){int before=lines;lines+=10;score+=1000;triggerMilestones(before);}
static bool clearLines(){
 int cleared=0;
 for(int y=BH-1;y>=0;y--){
  bool full=true;for(int x=0;x<BW;x++)if(!board[y][x]){full=false;break;}
  if(full){if(cleared<4)clearFxRows[cleared]=y;cleared++;}
 }
 if(!cleared)return false;
 clearFxCount=std::min(cleared,4);clearFxTimer=cleared==4?36:18;clearPending=true;clearBeforeLines=lines;
 sfxAudio.play(cleared==4?"romfs:/audio/sfx_heart_portal.mp3":((rand()&1)?"romfs:/audio/sfx_line_clear_a.mp3":"romfs:/audio/sfx_line_clear_b.mp3"));
 if(phobosRoute){
  if(cleared==4||rand()%3==0){char p[80];snprintf(p,sizeof(p),"romfs:/audio/react_phobos_tetris_%d.mp3",1+rand()%3);voiceAudio.play(p);}
 }else if(cleared==4){
  if(pieceEnabled[4]){char p[64];snprintf(p,sizeof(p),"romfs:/audio/react_will_tetris_%d.mp3",1+rand()%4);voiceAudio.play(p);}
  else if(rand()%100<55)voiceAudio.play("romfs:/audio/react_tetris_not_bad.mp3");
 }else if(rand()%100<5){
  voiceAudio.play("romfs:/audio/react_destroy_weak.mp3");
 }else{
  const char* element=nullptr;
  if(curType==0)element="earth";else if(curType==3)element="water";else if(curType==5)element="fire";else if(curType==6)element="air";
  if(element){char p[80];snprintf(p,sizeof(p),"romfs:/audio/react_%s_%d.mp3",element,1+rand()%3);voiceAudio.play(p);}
  else if(curType==2&&rand()%100<40)voiceAudio.play("romfs:/audio/react_caleb_clear.mp3");
  else if(curType==1){
   if(cleared==1&&rand()%100<68){char p[80];snprintf(p,sizeof(p),"romfs:/audio/react_blunk_one_%d.mp3",1+rand()%4);voiceAudio.play(p);}
   else if(cleared==2&&rand()%100<82){char p[80];snprintf(p,sizeof(p),"romfs:/audio/react_blunk_two_%d.mp3",1+rand()%3);voiceAudio.play(p);}
  }
 }
 return true;
}
static void finishPendingClear(){
 if(!clearPending)return;bool remove[BH]{};for(int i=0;i<clearFxCount;i++)if(clearFxRows[i]>=0&&clearFxRows[i]<BH)remove[clearFxRows[i]]=true;
 int write=BH-1;for(int y=BH-1;y>=0;y--)if(!remove[y]){if(write!=y)memcpy(board[write],board[y],sizeof(board[0]));write--;}
 while(write>=0){memset(board[write],0,sizeof(board[0]));write--;}
 int cleared=clearFxCount;clearPending=false;clearFxCount=0;lines+=cleared;static const int awards[5]={0,100,300,500,800};score+=awards[cleared];level=1+lines/10;
 triggerMilestones(clearBeforeLines);if(mode==GAME)spawn();
}
static void lockPiece(){bool horror=phobosRoute&&horrorPieces;for(int i=0;i<4;i++){int x,y;blockPos(curType,curRot,i,x,y);x+=curX;y+=curY;if(y>=0&&y<BH&&x>=0&&x<BW)board[y][x]=plainMode()?-(curType+1):encodeCell(curType,curRot,i,horror);}if(clearLines())curType=-1;else if(mode==GAME)spawn();}
static void hardDrop(){if(curType<0)return;while(fits(curType,curRot,curX,curY+1)){curY++;score+=2;}lockPiece();}
static void classicAdjusted(bool wasGrounded){if(!phobosFall&&wasGrounded&&lockResets<15){lockFrames=0;lockResets++;}}
static bool moveHorizontal(int dx){if(curType<0)return false;bool grounded=!fits(curType,curRot,curX,curY+1);if(fits(curType,curRot,curX+dx,curY)){curX+=dx;classicAdjusted(grounded);return true;}return false;}
static bool rotatePiece(int direction){if(curType<0)return false;rotationCount++;
 if(rotationCount>=5&&rotationHintBlockPieces<=0){int chance=rotationCount==5?12:rotationCount==6?24:rotationCount==7?38:rotationCount==8?55:72;if(rand()%100<chance&&playPhobosVoice("romfs:/audio/react_phobos_rotate_hint.mp3",false,true)){rotationCount=-999;rotationHintBlockPieces=6;}}
 bool grounded=!fits(curType,curRot,curX,curY+1);int nr=(curRot+direction+4)&3;static const int kick[]={0,-1,1,-2,2};for(int dx:kick)if(fits(curType,nr,curX+dx,curY)){curRot=nr;curX+=dx;classicAdjusted(grounded);return true;}return false;}
static void hold(){if(holdUsed||curType<0)return;if(holdType<0){holdType=curType;spawn();}else{std::swap(holdType,curType);curRot=0;curX=3;curY=-1;resetClassicLock();if(!fits(curType,curRot,curX,curY)){gameOver=true;gameOverIndex=0;saveRecord();handleGameOverVoice();}}holdUsed=true;holdCountWindow++;if(holdCountWindow>=7&&rand()%100<35){if(playPhobosVoice("romfs:/audio/react_phobos_hold_hint.mp3"))holdCountWindow=0;}}
static int gravityInterval(){int speedLines=lines+(std::max(1,std::min(5,startSpeed))-1)*25;if(speedLines<25)return 32;if(speedLines<50)return 28;if(speedLines<75)return 24;if(speedLines<100)return 20;if(speedLines<125)return 17;if(speedLines<150)return 14;if(speedLines<175)return 11;if(speedLines<200)return 9;return std::max(3,8-(speedLines-200)/50);}

static void drawPlain(float x,float y,float cell,int t,float z){x+=stereoX(z);u32 col=pieceColor(t);C2D_DrawRectSolid(x+1,y+1,z,cell-2,cell-2,col);C2D_DrawRectSolid(x+2,y+2,z+0.01f,cell-4,std::max(1.0f,cell*0.16f),C2D_Color32(255,255,255,115));}
static void drawSpriteCell(int idx,bool horror,float x,float y,float cell,float z){(horror?horrorCells:phaseCells).draw(idx,x,y,cell,z);}
static void drawBoardSlice(float x0,float y0,float cell,int yStart,int count){
 float frameX=x0+stereoX(0.24f),glassX=x0+stereoX(0.31f);
 // Four narrow strokes, never a filled rectangle: the castle must remain
 // physically visible through every cell of the playfield.
 u32 frame=C2D_Color32(105,62,145,220);float fw=BW*cell,fh=count*cell;
 C2D_DrawRectSolid(frameX-2,y0-2,0.24f,fw+4,2,frame);
 C2D_DrawRectSolid(frameX-2,y0+fh,0.24f,fw+4,2,frame);
 C2D_DrawRectSolid(frameX-2,y0,0.24f,2,fh,frame);
 C2D_DrawRectSolid(frameX+fw,y0,0.24f,2,fh,frame);
 u32 grid=C2D_Color32(210,175,245,78);
 for(int x=0;x<=BW;x++)C2D_DrawRectSolid(glassX+x*cell,y0,0.34f,1,count*cell,grid);
 for(int y=0;y<=count;y++)C2D_DrawRectSolid(glassX,y0+y*cell,0.34f,BW*cell,1,grid);
 if(!phobosFall&&curType>=0){
  int gy=curY;while(fits(curType,curRot,curX,gy+1))gy++;
  for(int i=0;i<4;i++){int bx,by;blockPos(curType,curRot,i,bx,by);int x=bx+curX,currentY=by+curY,landingY=by+gy;
   // A dotted centre trail remains readable over both bright and dark castle
   // details, while the landing ghost gets a translucent fill and 2px rim.
   int pathStart=std::max(currentY+1,yStart),pathEnd=std::min(landingY,yStart+count-1);float px=x0+x*cell+cell*.5f+stereoX(0.42f);
   for(int py=pathStart;py<pathEnd;py++){float yy=y0+(py-yStart)*cell+cell*.25f;for(float d=0;d<cell*.5f;d+=6.0f)C2D_DrawRectSolid(px-1,yy+d,0.42f,2,3,C2D_Color32(245,215,255,115));}
   if(landingY<yStart||landingY>=yStart+count)continue;float dx=x0+x*cell+stereoX(0.44f),dy=y0+(landingY-yStart)*cell;u32 fill=C2D_Color32(205,125,255,52),rim=C2D_Color32(250,225,255,205);C2D_DrawRectSolid(dx+2,dy+2,0.43f,cell-4,cell-4,fill);C2D_DrawRectSolid(dx+1,dy+1,0.44f,cell-2,2,rim);C2D_DrawRectSolid(dx+1,dy+cell-3,0.44f,cell-2,2,rim);C2D_DrawRectSolid(dx+1,dy+1,0.44f,2,cell-2,rim);C2D_DrawRectSolid(dx+cell-3,dy+1,0.44f,2,cell-2,rim);
  }
 }
 for(int yy=0;yy<count;yy++){int y=yStart+yy;for(int x=0;x<BW;x++){int code=board[y][x];if(!code)continue;if(code<0)drawPlain(x0+x*cell,y0+yy*cell,cell,cellType(code),0.5f);else drawSpriteCell((code&255)-1,(code&0x10000)!=0,x0+x*cell,y0+yy*cell,cell,0.5f);}}
 if(curType>=0)for(int i=0;i<4;i++){int x,y;blockPos(curType,curRot,i,x,y);x+=curX;y+=curY;if(y<yStart||y>=yStart+count)continue;float dx=x0+x*cell,dy=y0+(y-yStart)*cell;if(plainMode())drawPlain(dx,dy,cell,curType,0.6f);else drawSpriteCell(spriteIndex(curType,curRot,i),phobosRoute&&horrorPieces,dx,dy,cell,0.6f);}
 if(clearFxTimer>0){
  u32 bolt=phobosRoute?C2D_Color32(82,8,120,245):C2D_Color32(225,25,145,235);
  for(int i=0;i<clearFxCount;i++){int ry=clearFxRows[i];if(ry<yStart||ry>=yStart+count)continue;float rowY=y0+(ry-yStart)*cell;float sx=x0+stereoX(0.78f);u8 wash=(u8)(55+((frameCounter/3)&1)*35);C2D_DrawRectSolid(sx,rowY,0.76f,BW*cell,cell,C2D_Color32(phobosRoute?35:160,phobosRoute?0:8,phobosRoute?55:100,wash));float yy=rowY+cell*.45f;for(int k=0;k<6;k++){float xx=sx+k*(BW*cell/6.0f);C2D_DrawRectSolid(xx,yy+((k+frameCounter)&1?2:-2),0.78f,BW*cell/8.0f,2,bolt);}}
  if(clearFxCount==4){int lo=clearFxRows[0],hi=clearFxRows[0];for(int i=1;i<4;i++){lo=std::min(lo,clearFxRows[i]);hi=std::max(hi,clearFxRows[i]);}float centerRow=(lo+hi+1)*0.5f;if(centerRow>=yStart&&centerRow<yStart+count){float pulse=1.0f+0.08f*std::sin((36-clearFxTimer)*0.35f);float cx=x0+BW*cell*.5f,cy=y0+(centerRow-yStart)*cell;if(phobosRoute&&phobosEnabled)phobosGame.drawFit(cx-38*pulse,cy-49*pulse,76*pulse,98*pulse,0.96f);else endingHeart.drawFit(cx-39*pulse,cy-39*pulse,78*pulse,78*pulse,0.96f);}}
 }
}
static void drawMiniPiece(int t,float x,float y,float cell){if(t<0)return;int minx=9,miny=9;for(int i=0;i<4;i++){int bx,by;blockPos(t,0,i,bx,by);minx=std::min(minx,bx);miny=std::min(miny,by);}for(int i=0;i<4;i++){int bx,by;blockPos(t,0,i,bx,by);float dx=x+(bx-minx)*cell,dy=y+(by-miny)*cell;if(plainMode())drawPlain(dx,dy,cell,t,0.5f);else drawSpriteCell(spriteIndex(t,0,i),phobosRoute&&horrorPieces,dx,dy,cell,0.5f);}}

static std::string lowerAscii(std::string s){for(char& c:s)if((unsigned char)c<128)c=(char)std::tolower((unsigned char)c);return s;}
static bool any(const std::string& s,std::initializer_list<const char*> values){for(const char* v:values)if(s==v)return true;return false;}
static void saveWinnerMusic(){
 if(mode!=WINNER||!audio.playing)return;
 winnerMusicSaved=true;winnerMusicPath=audio.path;winnerMusicSample=audio.tell();winnerMusicLooping=audio.looping;
}
static void resumeWinnerMusic(){
 music.autoAdvance=false;
 if(winnerMusicSaved&&!winnerMusicPath.empty())audio.playFrom(winnerMusicPath.c_str(),winnerMusicSample,winnerMusicLooping);
 else audio.play("romfs:/audio/music_winner_choice.mp3",true);
 winnerMusicSaved=false;
}
static void startVideo(int kind,Mode after){if(after==WINNER)saveWinnerMusic();videoKind=kind;videoTick=0;videoLoaded=-1;videoCount=kind==1?214:35;videoFps=kind==1?6:12;returnMode=after;videoFrame.free();mode=VIDEO_MODE;music.autoAdvance=false;audio.play(kind==1?"romfs:/video/matrix.mp3":"romfs:/video/porn.mp3");}
static void startVtd(){returnMode=mode;mode=VTD_MODE;music.autoAdvance=false;if(!audio.play((rand()%2)?"romfs:/audio/vtd_1.mp3":"romfs:/audio/vtd_2.mp3"))running=false;}
static void chooseWinner(){
 if(winnerChoice==1&&!phobosEnabled)winnerChoice=0;
 guardiansRoute=winnerChoice==0;phobosRoute=!guardiansRoute;horrorPieces=phobosRoute&&(rand()%100<80);
 if(guardiansRoute||!horrorPieces)for(int y=0;y<BH;y++)for(int x=0;x<BW;x++)if(board[y][x])board[y][x]=-(cellType(board[y][x])+1);
 mode=ROUTE_VICTORY;victoryTimer=0;music.autoAdvance=false;audio.stop();voiceAudio.stop();winnerMusicSaved=false;
 if(guardiansRoute)voiceAudio.play("romfs:/audio/will_reverse.mp3");
 else{char p[80];snprintf(p,sizeof(p),"romfs:/audio/react_phobos_tetris_%d.mp3",1+rand()%3);voiceAudio.play(p);}
}
static void continueRouteVictory(){mode=GAME;music.autoAdvance=true;music.start(phobosRoute?4:3);}
static void handleGameplayCode(const std::string& s){
 if(any(s,{"q","й"})){developerAddLines();return;}
 if(any(s,{"witch","vich","витч","вич","guardians","стражницы","чародейки","kandrakar","кондракар"})){memset(board,0,sizeof(board));return;}
 if(any(s,{"matrix","матрица"})){gameplayMatrixTimer=60*8;return;}
 if(any(s,{"jetix","jtx","джетикс","джт"})){gameplayJetixTimer=60*6;return;}
 if(any(s,{"vtd","втд","valentin","valentine","валентин"})){gameplayVtdTimer=60*300;music.autoAdvance=false;audio.play((rand()%2)?"romfs:/audio/vtd_1.mp3":"romfs:/audio/vtd_2.mp3");return;}
 if(any(s,{"porn","порн"})){returnMode=GAME;mode=PORN_GALLERY;pornImage=rand()%2;music.autoAdvance=false;if(rand()%5==0)voiceAudio.play("romfs:/audio/voice_porn.mp3");return;}
 if(any(s,{"phobos","fobos","фобос"})&&phobosEnabled){voiceAudio.play("romfs:/audio/voice_phobos.mp3");return;}
}
static void handleWinnerCode(const std::string& s){
 if(any(s,{"matrix","матрица"})){startVideo(1,WINNER);return;}
 if(any(s,{"vtd","втд","valentin","valentine","валентин"})){startVtd();return;}
 if(any(s,{"jetix","jtx","джетикс","джт"})){saveWinnerMusic();returnMode=WINNER;mode=JETIX_MODE;secretTimer=60*6;return;}
 if(any(s,{"gpt","chatgpt","гпт"})){saveWinnerMusic();returnMode=WINNER;mode=CARD_MODE;secretTimer=0;return;}
 if(any(s,{"suno","suna","суно","суна"})){saveWinnerMusic();returnMode=WINNER;mode=CARD_MODE;secretTimer=1;return;}
 if(any(s,{"porn","порн"})){startVideo(2,WINNER);return;}
 if(any(s,{"artem","артем","артём","artmka"})){story200ArtemCount++;return;}
 if(any(s,{"witch","vich","витч","вич","guardians","стражницы","чародейки","will","irma","cornelia","taranee","hay lin"})){winnerChoice=0;chooseWinner();return;}
 if(any(s,{"phobos","fobos","фобос"})&&phobosEnabled){winnerChoice=1;chooseWinner();return;}
 if(any(s,{"me","we","я","мы"})&&phobosEnabled){winnerChoice=1;chooseWinner();return;}
}
static void handleCode(const std::string& raw){std::string s=lowerAscii(raw);if(mode==GAME)handleGameplayCode(s);else if(mode==WINNER)handleWinnerCode(s);}
static void openCodeKeyboard(){char buf[64]={0};SwkbdState sw;swkbdInit(&sw,SWKBD_TYPE_NORMAL,1,32);swkbdSetHintText(&sw,"...");swkbdSetButton(&sw,SWKBD_BUTTON_RIGHT,"OK",true);swkbdSetFeatures(&sw,SWKBD_DEFAULT_QWERTY|SWKBD_ALLOW_HOME);if(swkbdInputText(&sw,buf,sizeof(buf))!=SWKBD_BUTTON_NONE)handleCode(buf);}

static const char* EN_KEYS[3][11]={{"Q","W","E","R","T","Y","U","I","O","P",nullptr},{"A","S","D","F","G","H","J","K","L",nullptr,nullptr},{"Z","X","C","V","B","N","M",nullptr,nullptr,nullptr,nullptr}};
static const char* RU_KEYS[3][12]={{"й","ц","у","к","е","н","г","ш","щ","з","х",nullptr},{"ф","ы","в","а","п","р","о","л","д","ж","э",nullptr},{"я","ч","с","м","и","т","ь","б","ю",nullptr,nullptr,nullptr}};
static const int EN_COUNT[3]={10,9,7},RU_COUNT[3]={11,11,9};

static void popUtf8(std::string& s){if(s.empty())return;size_t p=s.size()-1;while(p>0&&((unsigned char)s[p]&0xC0)==0x80)p--;s.erase(p);}
static void drawKey(float x,float y,float w,float h,const char* label){C2D_DrawRectSolid(x,y,0.32f,w,h,C2D_Color32(66,35,91,245));C2D_DrawRectSolid(x+1,y+1,0.33f,w-2,h-2,C2D_Color32(30,18,45,245));centerText(x+w/2,y+7,0.42f,WHITE,label);}
static void drawVirtualKeyboard(){
 C2D_TargetClear(botTarget,C2D_Color32(10,6,18,255));beginBottom();
 C2D_DrawRectSolid(10,7,0.2f,300,29,C2D_Color32(25,13,38,255));
 std::string shown=codeBuffer.empty()?"_":codeBuffer;centerText(160,13,0.42f,ACCENT,shown.c_str());
 for(int row=0;row<3;row++){
  int n=keyboardRussian?RU_COUNT[row]:EN_COUNT[row];float w=keyboardRussian?24.0f:28.0f,gap=2.0f;float x=(320-(n*w+(n-1)*gap))/2.0f;float y=45+row*39;
  for(int i=0;i<n;i++)drawKey(x+i*(w+gap),y,w,33,keyboardRussian?RU_KEYS[row][i]:EN_KEYS[row][i]);
 }
 drawKey(10,169,68,38,keyboardRussian?"EN":"RU");drawKey(86,169,85,38,"DELETE");drawKey(179,169,131,38,"ENTER");
 centerText(160,218,0.29f,DIM,"Y: language   X: delete   START: enter");
}
static void appendKey(const char* s){if(codeBuffer.size()<40){std::string v=s;for(char& c:v)if((unsigned char)c<128)c=(char)std::tolower((unsigned char)c);codeBuffer+=v;}}
static void handleVirtualKeyboardTouch(int px,int py){
 for(int row=0;row<3;row++){
  int n=keyboardRussian?RU_COUNT[row]:EN_COUNT[row];float w=keyboardRussian?24.0f:28.0f,gap=2.0f;float x=(320-(n*w+(n-1)*gap))/2.0f;float y=45+row*39;
  if(py>=y&&py<y+33){int i=(int)((px-x)/(w+gap));if(i>=0&&i<n&&px>=x+i*(w+gap)&&px<x+i*(w+gap)+w){appendKey(keyboardRussian?RU_KEYS[row][i]:EN_KEYS[row][i]);return;}}
 }
 if(py>=169&&py<207){if(px>=10&&px<78){keyboardRussian=!keyboardRussian;return;}if(px>=86&&px<171){popUtf8(codeBuffer);return;}if(px>=179&&px<310){std::string entered=codeBuffer;codeBuffer.clear();if(!entered.empty())handleCode(entered);return;}}
}

static void drawBoot(){
 C2D_DrawRectSolid(0,0,0.1f,400,240,C2D_Color32(0,0,0,255));
 centerText(200,20,0.75f,RED,"WARNING!!!!!");centerText(200,54,0.52f,WHITE,"ALPHA CHANNEL TROUBLE");
 centerText(200,82,0.66f,C2D_Color32(255,200,98,255),"18+");centerText(200,116,0.53f,ACCENT,"REMEMBER:");
 centerText(200,148,0.37f,WHITE,"JETIX  WITCH  PHOBOS  Q  MATRIX");centerText(200,170,0.37f,WHITE,"VTD  PORN");
 centerText(200,202,0.30f,DIM,"PHOBOS CHARACTER HAS ESCAPED CONTROL.");
 C2D_TargetClear(botTarget,C2D_Color32(0,0,0,255));beginBottom();centerText(160,96,0.52f,DIM,"PRESS ANY KEY");centerText(160,132,0.38f,DIM,"TO CONTINUE");
}

static void drawMenu(){bgMenu.drawCover(0,0,TOP_W,H,0.04f);C2D_DrawRectSolid(-10+stereoX(0.16f),0,0.16f,420,H,C2D_Color32(0,0,0,90));if(phobosEnabled)phobosMenu.drawFit(250,18,145,216,0.88f);float oz=textZ,od=textDepth;textZ=0.76f;textDepth=0.76f;drawText(18,20,0.72f,ACCENT,"W.I.T.C.H. TETRIS 3DS");drawText(20,48,0.43f,WHITE,"NATIVE STORY TEST 15");const char* items[]={"NEW GAME","RECORDS","CUTSCENES","SETTINGS","EXIT"};for(int i=0;i<5;i++){if(i==menuIndex)C2D_DrawRectSolid(18+stereoX(0.58f),76+i*31,0.58f,205,26,C2D_Color32(95,45,120,220));drawText(28,78+i*31,0.50f,i==menuIndex?WHITE:DIM,"> %s",items[i]);}textZ=oz;textDepth=od;C2D_TargetClear(botTarget,BG);beginBottom();centerText(160,25,0.6f,ACCENT,"MAIN MENU");centerText(160,70,0.45f,WHITE,"D-Pad: select   A: open");centerText(160,105,0.39f,DIM,"Character choice unlocks at 200 lines");centerText(160,185,0.4f,DIM,"START: exit");}
static void drawRecords(){
 drawText(18,14,0.70f,ACCENT,"RECORDS");drawText(25,47,0.35f,DIM,"#      LINES        SCORE");
 if(records.empty())centerText(200,108,0.48f,DIM,"NO RECORDS YET");
 for(size_t i=0;i<records.size()&&i<10;i++)drawText(28,68+(float)i*15,0.34f,i==0?ACCENT:WHITE,"%2d      %4d        %7d",(int)i+1,records[i].lines,records[i].score);
 C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));beginBottom();
 if(recordsConfirmReset){centerText(160,42,0.54f,RED,"CLEAR ALL RECORDS?");const char* opts[]={"YES","NO"};for(int i=0;i<2;i++){float x=35+i*145;if(i==recordsIndex)C2D_DrawRectSolid(x,112,0.4f,110,38,C2D_Color32(100,50,130,245));centerText(x+55,122,0.47f,i==recordsIndex?WHITE:DIM,opts[i]);}centerText(160,184,0.34f,DIM,"LEFT / RIGHT + A");}
 else{const char* opts[]={"RESET RECORDS","BACK"};for(int i=0;i<2;i++){if(i==recordsIndex)C2D_DrawRectSolid(45,75+i*55,0.4f,230,40,C2D_Color32(100,50,130,245));centerText(160,86+i*55,0.46f,i==recordsIndex?WHITE:DIM,opts[i]);}centerText(160,205,0.32f,DIM,"B: main menu");}
}
static void drawGallery(){drawText(18,18,0.72f,ACCENT,"CUTSCENES");const char* items[]={"INTRO","100 LINES","200 LINES","GUARDIANS ENDING","BACK"};for(int i=0;i<5;i++){if(i==galleryIndex)C2D_DrawRectSolid(20,62+i*34,0.2f,360,28,C2D_Color32(85,40,115,230));drawText(30,65+i*34,0.5f,i==galleryIndex?WHITE:DIM,"%s",items[i]);}C2D_TargetClear(botTarget,BG);beginBottom();centerText(160,70,0.5f,WHITE,"A: play");centerText(160,115,0.42f,DIM,"B: main menu");}
static void drawSettings(){drawText(18,18,0.72f,ACCENT,settingsRoster?"FIGURES / PHOBOS":"OPTIONS");if(!settingsRoster){const char* names[]={"TETRIS LAYOUT","PIECE FALL MODE","START SPEED","FIGURE ROSTER","BACK"};for(int i=0;i<5;i++){if(i==settingsIndex)C2D_DrawRectSolid(15+stereoX(0.54f),55+i*35,0.54f,370,30,C2D_Color32(85,40,115,230));drawText(25,60+i*35,0.43f,i==settingsIndex?WHITE:DIM,"%s",names[i]);if(i==0)drawText(250,60+i*35,0.40f,ACCENT,"%s",dualScreen?"DUAL":"COMPACT");if(i==1)drawText(250,60+i*35,0.40f,ACCENT,"%s",phobosFall?"PHOBOS":"CLASSIC");if(i==2)drawText(320,60+i*35,0.40f,ACCENT,"%d",startSpeed);}}else{const char* rn[]={"I  CORNELIA","O  BLUNK","T  CALEB","S  IRMA","Z  WILL","J  TARANEE","L  HAY LIN","PHOBOS","BACK"};for(int i=0;i<9;i++){float y=44+i*21;if(i==settingsIndex)C2D_DrawRectSolid(16+stereoX(0.54f),y-2,0.54f,368,20,C2D_Color32(85,40,115,230));drawText(25,y,0.37f,i==settingsIndex?WHITE:DIM,"%s",rn[i]);if(i<8)drawText(310,y,0.35f,(i<7?pieceEnabled[i]:phobosEnabled)?GREEN:RED,(i<7?pieceEnabled[i]:phobosEnabled)?"ON":"OFF");}}C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));beginBottom();centerText(160,35,0.52f,WHITE,"A / LEFT / RIGHT: change");centerText(160,82,0.42f,DIM,settingsRoster?"All seven figures may be disabled":"START SPEED changes gravity only");centerText(160,135,0.4f,ACCENT,settingsRoster?"Phobos remains independently optional":"A+B toggles dual layout in game");centerText(160,190,0.4f,DIM,"B: back");}

static void drawPauseTop(){
 // Three visibly separate planes: dimmer, selection slab and text.
 C2D_DrawRectSolid(-20+stereoX(0.82f),0,0.82f,440,240,C2D_Color32(0,0,0,218));
 const char* p[]={"CONTINUE","RESTART","MAIN MENU"};
 for(int i=0;i<3;i++)if(i==pauseIndex)C2D_DrawRectSolid(76+stereoX(0.86f),70+i*48,0.86f,248,38,C2D_Color32(100,50,130,255));
 float oldZ=textZ,oldDepth=textDepth;textZ=0.96f;textDepth=0.96f;
 centerText(200,24,0.68f,ACCENT,"PAUSED");for(int i=0;i<3;i++)centerText(200,79+i*48,0.54f,i==pauseIndex?WHITE:DIM,p[i]);
 textZ=oldZ;textDepth=oldDepth;
}
static void drawPauseBottom(){C2D_TargetClear(botTarget,C2D_Color32(8,4,14,255));beginBottom();bgGame[lines<100?0:(guardiansRoute?2:1)].drawCover(0,0,320,240,0.08f);C2D_DrawRectSolid(0,0,0.2f,320,240,C2D_Color32(0,0,0,185));centerText(160,91,0.52f,ACCENT,"PAUSED");centerText(160,137,0.36f,DIM,"THE MENU IS ON TOP");}
static void drawPhobosDeletedWin(){
 C2D_DrawRectSolid(-20+stereoX(0.96f),0,0.96f,440,240,C2D_Color32(0,0,0,230));float oz=textZ,od=textDepth;textZ=0.995f;textDepth=0.995f;centerText(200,66,0.76f,ACCENT,"YOU WIN");centerText(200,123,0.52f,WHITE,"PHOBOS DELETED");centerText(200,188,0.31f,DIM,"PRESS ANY BUTTON");textZ=oz;textDepth=od;
 C2D_TargetClear(botTarget,C2D_Color32(7,3,12,255));beginBottom();centerText(160,91,0.52f,ACCENT,"SYSTEM ERROR");centerText(160,137,0.36f,DIM,"PHOBOS IS MISSING");
}
static void drawEmptyRosterBored(){
 C2D_DrawRectSolid(-20+stereoX(0.96f),0,0.96f,440,240,C2D_Color32(0,0,0,218));float oz=textZ,od=textDepth;textZ=0.995f;textDepth=0.995f;centerText(200,70,0.58f,ACCENT,"PHOBOS");centerText(200,126,0.58f,WHITE,"МНЕ СКУЧНО.");centerText(200,188,0.31f,DIM,"THE GAME IS ENDING");textZ=oz;textDepth=od;
 C2D_TargetClear(botTarget,C2D_Color32(8,4,14,255));beginBottom();centerText(160,100,0.54f,ACCENT,"PHOBOS GOT BORED");
}
static void drawGameOverTop(){
 C2D_DrawRectSolid(stereoX(0.90f),0,0.90f,400,240,C2D_Color32(0,0,0,205));const char* p[]={"RESTART","MAIN MENU"};
 for(int i=0;i<2;i++)if(i==gameOverIndex)C2D_DrawRectSolid(95+stereoX(0.94f),118+i*42,0.94f,210,32,C2D_Color32(100,50,130,255));
 float oldZ=textZ,oldDepth=textDepth;textZ=0.98f;textDepth=0.98f;centerText(200,36,0.85f,emptyRosterBored?ACCENT:RED,emptyRosterBored?"ИГРА ЗАКОНЧЕНА":"GAME OVER");if(emptyRosterBored)centerText(200,78,0.48f,WHITE,"ФОБОС ЗАСКУЧАЛ");else drawText(124,78,0.38f,WHITE,"LINES %d     SCORE %d",lines,score);for(int i=0;i<2;i++)centerText(200,124+i*42,0.52f,i==gameOverIndex?WHITE:DIM,p[i]);textZ=oldZ;textDepth=oldDepth;
}
static void drawGameOverBottom(){C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));beginBottom();bgGame[0].drawCover(0,0,320,240,0.1f);C2D_DrawRectSolid(0,0,0.2f,320,240,C2D_Color32(0,0,0,175));centerText(160,84,0.52f,RED,"GAME OVER");centerText(160,128,0.40f,WHITE,"UP / DOWN + A");centerText(160,205,0.31f,DIM,"THE CHOICE IS ON TOP");}
static void drawGameplayEffects(float width,float height,bool topScreen){
 if(gameplayMatrixTimer>0||gameplayVtdTimer>0){
  C2D_DrawRectSolid(stereoX(0.69f),0,0.69f,width,height,C2D_Color32(0,35,12,24));
  float oldDepth=textDepth;textDepth=0.78f;
  int cols=(int)(width/25.0f);
  for(int i=0;i<cols;i++){float y=(float)((frameCounter*(2+i%3)+i*41)%330)-90;for(int j=0;j<6;j++){u32 c=j==0?C2D_Color32(210,255,220,225):C2D_Color32(45,255,105,150-j*16);char glyph[2]={(char)('A'+(i*7+j*11+frameCounter/5)%26),0};drawText(5+i*25,y+j*16,0.27f,c,"%s",glyph);}}
  textDepth=oldDepth;
 }
 if(gameplayJetixTimer>0){
  int sx=std::max(1,(int)width-74),sy=std::max(1,(int)height-58);int tx=(frameCounter*3)%(sx*2),ty=(frameCounter*2+37)%(sy*2);float x=(float)(tx>sx?sx*2-tx:tx),y=(float)(ty>sy?sy*2-ty:ty);jetixLogo.drawFit(x,y,74,56,0.90f);
  for(int i=0;i<24;i++){float fx=(float)((i*53+frameCounter*(1+i%4))%(int)width),fy=(float)((i*29+frameCounter*(2+i%3))%(int)height);u32 c=(i%3==0)?C2D_Color32(255,70,170,210):(i%3==1)?C2D_Color32(255,225,50,210):C2D_Color32(70,210,255,210);C2D_DrawRectSolid(fx+stereoX(0.74f),fy,0.74f,3,3,c);}
 }
 if(topScreen&&gameplayVtdTimer>0){if(dualScreen)vtdObs.drawFit(312,145,82,92,0.92f);else vtdObs.drawFit(286,31,109,202,0.92f);}
}
static void drawGame(){
 int bg=lines<100?0:(guardiansRoute?2:1);
 if(dualScreen)bgDual[bg].drawDualContinuation(false,0.08f);else bgGame[bg].drawCover(0,0,400,240,0.08f);
 C2D_DrawRectSolid(stereoX(0.15f),0,0.15f,400,240,C2D_Color32(0,0,0,12));
 if(dualScreen){drawBoardSlice(74,0,24,0,10);drawText(4,8,0.39f,ACCENT,"HOLD");if(holdType>=0)drawMiniPiece(holdType,3,38,9);drawText(326,8,0.39f,ACCENT,"NEXT");drawMiniPiece(nextType,328,38,9);char hudScore[32],hudLines[24];snprintf(hudScore,sizeof(hudScore),"S %d",score);snprintf(hudLines,sizeof(hudLines),"L %d",lines);centerText(357,100,0.29f,WHITE,hudScore);centerText(357,123,0.29f,DIM,hudLines);if(gameplayVtdTimer<=0&&phobosEnabled&&!guardiansRoute)phobosGame.drawFit(318,150,78,88,0.88f);}
 else{if(gameplayVtdTimer<=0&&phobosEnabled&&!guardiansRoute)phobosGame.drawFit(290,32,105,200,0.88f);drawBoardSlice(118,18,10,0,20);drawText(10,20,0.45f,ACCENT,"HOLD");if(holdType>=0)drawMiniPiece(holdType,15,52,10);drawText(238,20,0.45f,ACCENT,"NEXT");drawMiniPiece(nextType,245,52,10);drawText(8,128,0.38f,WHITE,"SCORE %d",score);drawText(8,150,0.38f,WHITE,"LINES %d",lines);}
 if(phobosDeletedWin){drawPhobosDeletedWin();return;}
 if(paused){drawPauseTop();drawPauseBottom();return;}
 if(emptyRosterBored&&!gameOver){drawEmptyRosterBored();return;}
 if(gameOver){drawGameOverTop();drawGameOverBottom();return;}
 drawGameplayEffects(400,240,true);
 C2D_TargetClear(botTarget,BG);beginBottom();
 if(dualScreen){bgDual[bg].drawDualContinuation(true,0.08f);C2D_DrawRectSolid(0,0,0.15f,320,240,C2D_Color32(0,0,0,12));drawBoardSlice(34,0,24,10,10);C2D_DrawRectSolid(2,198,0.9f,36,38,C2D_Color32(90,45,120,235));centerText(20,207,0.29f,WHITE,"KB");}
 else{bgGame[bg].drawCover(0,0,320,240,0.08f);C2D_DrawRectSolid(0,0,0.15f,320,240,C2D_Color32(0,0,0,145));centerText(160,15,0.46f,ACCENT,"CONTROLS");drawText(16,52,0.38f,WHITE,"D-Pad move   UP/A/B rotate");drawText(16,78,0.38f,WHITE,"Y hard drop       X hold");drawText(16,104,0.38f,WHITE,"L/R original phase shuffle");drawText(16,130,0.38f,WHITE,"START/SELECT pause");drawText(16,156,0.36f,DIM,"A+B: dual-screen layout");C2D_DrawRectSolid(178,194,0.3f,126,36,C2D_Color32(90,45,120,235));centerText(241,203,0.42f,WHITE,"KEYBOARD");}
 drawGameplayEffects(320,240,false);
}

static void drawCharacterLine(Art* set,float y){for(int i=0;i<7;i++)set[i].drawFit(10+i*56,y,48,145,0.3f);}
static void drawHallGroup(bool finalForms=false,bool featuredWill=false){
 introHall.drawCover(0,0,400,240,0.1f);C2D_DrawRectSolid(0,0,0.12f,400,240,C2D_Color32(28,0,45,78));
 // Phobos stays behind the team and no longer intersects Caleb's silhouette.
 introPhobos.drawFit(172,34,56,150,0.20f);
 Art* set=finalForms?introFinal:introNormal;
 set[0].drawFit(12,65,66,155,0.30f);   // Cornelia
 set[3].drawFit(60,66,64,154,0.31f);   // Irma
 set[5].drawFit(275,66,64,154,0.31f);  // Taranee
 set[6].drawFit(322,65,66,155,0.30f);  // Hay Lin
 set[2].drawFit(234,72,62,148,0.38f);  // Caleb
 set[1].drawFit(342,151,48,72,0.48f);  // Blunk
 if(featuredWill)l100Heart.drawFit(89,28,128,196,0.47f);
 else set[4].drawFit(102,62,66,162,0.47f);
}
static void drawTerminal100(){
 int t=story100Tick;
 if(t<78){
  int jitter=((t/3)%3)-1;bgGame[0].drawCover((float)jitter*5,0,400,240,0.08f);drawBoardSlice(118+jitter*3,18,10,0,20);
  for(int i=0;i<9;i++){int y=(i*31+t*7)%240;u32 c=(i+t)%3?C2D_Color32(12,0,20,120):C2D_Color32(190,20,230,155);C2D_DrawRectSolid(stereoX(0.84f),y,0.84f,400,1+(i&3),c);}
  if((t/5)%4==1)C2D_DrawRectSolid(stereoX(0.91f),0,0.91f,400,240,C2D_Color32(255,255,255,42));
  float oz=textZ,od=textDepth;textZ=0.96f;textDepth=0.96f;centerText(200,104,0.55f,RED,"MERIDIAN CONTROL FAILURE");textZ=oz;textDepth=od;return;
 }
 if(t<120){C2D_DrawRectSolid(0,0,0.1f,400,240,C2D_Color32(0,0,0,255));if(t>105&&((t/4)&1))centerText(200,109,0.42f,C2D_Color32(110,0,145,220),"SIGNAL LOST");return;}
 int tt=t-120;C2D_DrawRectSolid(0,0,0.1f,400,240,C2D_Color32(0,0,0,255));
 const char* line[]={"MERIDIAN CONTROL SYSTEM","PHOBOS SPELL ENGINE v1.0","> checking guardian bindings...","WILL       UNSTABLE","IRMA       UNSTABLE","CORNELIA   UNSTABLE","TARANEE    UNSTABLE","HAY LIN    UNSTABLE","CALEB      UNSTABLE","BLUNK      UNSTABLE","> integrity: 49%","> spell warranty expired 1847 years ago","FATAL ERROR: CONTROL OVER GUARDIANS LOST."};
 int visible=std::min(13,tt/13+1);for(int i=0;i<visible;i++){u32 col=i==12?RED:(i==11?C2D_Color32(235,190,85,255):GREEN);drawText(15,10+i*16,i<2?0.34f:0.285f,col,"%s",line[i]);}
 if(tt>105){if(story100Variant==4)terminalHeartMask.drawFit(276,70,112,145,0.72f);else if(story100Variant==5)terminalJetixMask.drawFit(275,65,115,150,0.72f);else if(story100Variant==6)endingHeart.drawFit(286,92,94,94,0.72f);else if(story100Variant==7)phobosGame.drawFit(291,57,90,160,0.72f);}
 if(tt>150){static const char* joke[]={"> ERROR: crown.exe stopped responding","> TODO: blame the Guardians","> rebooting throne room...","# absolutely not a secret code"};drawText(15,220,0.27f,C2D_Color32(145,255,190,255),"%s",joke[story100Variant&3]);}
 if(t>=440&&((frameCounter/24)&1))centerText(200,204,0.33f,WHITE,"A / TOUCH: CONTINUE");
}
static void drawCutscene(){
 if(cutsceneStage==0){introCastle.drawCover(0,0,400,240);C2D_DrawRectSolid(0,0,0.1f,400,240,C2D_Color32(0,0,0,85));if(cutscenePage==0){introPhobos.drawFit(258,20,130,215,0.38f);drawText(15,18,0.56f,ACCENT,"PHOBOS CASTLE");drawText(15,192,0.38f,WHITE,"THE SPELL HAS ALREADY BEGUN.");}else if(cutscenePage==1){drawHallGroup(false,false);centerText(200,10,0.44f,WHITE,"THE GUARDIANS ARRIVE");}else if(cutscenePage==2){drawHallGroup(true,false);centerText(200,10,0.42f,ACCENT,"PHOBOS TURNS THEM INTO PIECES");}else{introHall.drawCover(0,0,400,240,0.1f);introPhobos.drawFit(120,10,160,220,0.42f);centerText(200,205,0.40f,WHITE,"THIS GAME HAS ONLY BEGUN");}}
 else if(cutsceneStage==100){if(cutscenePage==0){drawTerminal100();}else if(cutscenePage==1){drawHallGroup(false,false);centerText(200,8,0.46f,ACCENT,"100 LINES - RESISTANCE");centerText(200,210,0.34f,WHITE,"THE GUARDIANS REMEMBER THEMSELVES");}else if(cutscenePage==2){introHall.drawCover(0,0,400,240,0.1f);l100Will.drawFit(18,25,172,195,0.42f);l100Phobos.drawFit(218,17,164,205,0.34f);centerText(200,8,0.43f,ACCENT,"THE SPELL IS LOSING CONTROL");}else{drawHallGroup(false,true);centerText(200,207,0.35f,WHITE,"PHOBOS: YOU WILL PAY FOR THIS!");}}
 else{if(cutscenePage==0){drawHallGroup(true,false);centerText(200,10,0.45f,ACCENT,"200 LINES - THE SPELL BREAKS");centerText(200,207,0.34f,WHITE,"THE GUARDIANS RETURN");}else if(cutscenePage==1){drawHallGroup(false,true);centerText(200,207,0.36f,WHITE,"WE ARE TOGETHER AGAIN!");}else if(cutscenePage==2){introHall.drawCover(0,0,400,240,0.1f);l100Phobos.drawFit(105,8,190,220,0.45f);centerText(200,205,0.34f,RED,"PHOBOS: NO... IMPOSSIBLE!");}else{introHall.drawCover(0,0,400,240,0.1f);C2D_DrawRectSolid(0,0,0.2f,400,240,C2D_Color32(35,0,45,145));drawHallGroup(false,false);C2D_DrawRectSolid(0,0,0.7f,400,240,C2D_Color32(15,0,25,105));centerText(200,76,0.70f,ACCENT,"WHO WINS?");centerText(200,135,0.40f,WHITE,"THE DECISION IS YOURS");}}
 C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));beginBottom();centerText(160,72,0.52f,WHITE,"A / TOUCH: next frame");centerText(160,120,0.42f,DIM,"B / START: skip scene");centerText(160,178,0.34f,ACCENT,"This scene contains several frames");
}
static void drawWinner(){introHall.drawCover(0,0,400,240,0.1f);C2D_DrawRectSolid(0,0,0.16f,400,240,C2D_Color32(0,0,0,150));if(winnerChoice==0)endingHeart.drawFit(145,3,110,94,0.38f);else if(phobosEnabled)phobosMenu.drawFit(162,0,76,105,0.52f);centerText(200,94,0.68f,ACCENT,"WHO WINS?");if(story200ArtemCount==1)centerText(200,121,0.27f,WHITE,"СПАСИБО, НО ДЕЛАЙ ВЫБОР.");else if(story200ArtemCount==2)centerText(200,121,0.29f,WHITE,"ПРОСТО ДЕЛАЙ ВЫБОР.");const char* opts[]={"GUARDIANS","PHOBOS"};for(int i=0;i<2;i++){float x=24+i*190;if(i==winnerChoice)C2D_DrawRectSolid(x,142,0.7f,162,45,C2D_Color32(95,45,120,235));centerText(x+81,154,0.48f,i==winnerChoice?WHITE:DIM,opts[i]);}centerText(200,207,0.30f,DIM,"LEFT / RIGHT + A");drawVirtualKeyboard();}
static void drawRouteVictory(){
 introHall.drawCover(0,0,400,240,0.08f);C2D_DrawRectSolid(0,0,0.14f,400,240,C2D_Color32(25,0,42,85));if(guardiansRoute)endingHeart.drawFit(158,12,84,76,0.42f);else phobosMenu.drawFit(151,8,98,142,0.67f);
 static const float px[7]={18,334,218,66,145,272,105};static const float py[7]={82,153,91,86,52,86,82};static const float pw[7]={62,48,62,62,86,62,62};static const float ph[7]={138,68,132,136,168,136,138};
 for(int i=0;i<7;i++)if(pieceEnabled[i]){float jump=guardiansRoute?std::max(0.0f,std::sin(victoryTimer*0.085f-i*0.7f))*8.0f:0.0f;float split=phobosRoute&&!horrorPieces?std::min(90,victoryTimer)*((i<4)?-0.55f:0.55f):0.0f;if(guardiansRoute||horrorPieces||victoryTimer<150)introNormal[i].drawFit(px[i]+split,py[i]-jump,pw[i],ph[i],0.48f+(i%3)*0.03f);}
 centerText(200,8,0.54f,phobosRoute?RED:ACCENT,phobosRoute?"PHOBOS WON!":"THE GUARDIANS WON!");centerText(200,214,0.31f,WHITE,voiceAudio.playing?(phobosRoute?"PHOBOS' REVERSE MESSAGE...":"WILL'S REVERSE MESSAGE..."):"A / B: CONTINUE TETRIS");
 C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));beginBottom();centerText(160,48,0.55f,phobosRoute?RED:ACCENT,"VICTORY");centerText(160,94,0.40f,WHITE,phobosRoute?(horrorPieces?"HORROR PIECES - 80%":"ORDINARY PIECES - 20%"):"THE SPELL IS BROKEN");centerText(160,139,0.35f,DIM,"Reverse audio is playing");centerText(160,179,0.35f,DIM,"Tetris continues afterwards");
}
static void drawEnding(){
 if(endingFrame.sheet)endingFrame.drawCover(0,0,400,240,0.30f);else endingWitch.drawCover(0,0,400,240,0.30f);
 if(endingFinished){C2D_DrawRectSolid(0,0,0.72f,400,240,C2D_Color32(0,0,0,105));centerText(200,82,0.70f,ACCENT,"THANK YOU FOR PLAYING");centerText(200,154,0.38f,WHITE,"A / B / TOUCH: MENU");}
 C2D_TargetClear(botTarget,C2D_Color32(0,0,0,255));beginBottom();
}
static void drawRoomEntry(){C2D_DrawRectSolid(0,0,0.1f,400,240,C2D_Color32(0,0,0,255));float pulse=0.48f+0.05f*std::sin(roomEntryTimer*0.08f);centerText(200,82,0.66f,RED,"PHOBOS");centerText(200,132,pulse,WHITE,"THE DOOR CLOSES BEHIND YOU");centerText(200,188,0.31f,DIM,"REVERSE MESSAGE");C2D_TargetClear(botTarget,C2D_Color32(0,0,0,255));beginBottom();centerText(160,92,0.46f,ACCENT,"ENTERING PHOBOS ROOM");centerText(160,137,0.34f,DIM,"There is no way back");}
static void drawRoom(){roomBg.drawCover(0,0,400,240,0.08f);roomPoses[roomPose].drawFit(225,18,165,215,0.84f);roomFg.drawFit(0,0,400,240,0.58f);C2D_TargetClear(botTarget,C2D_Color32(15,6,20,255));beginBottom();centerText(160,20,0.58f,ACCENT,"PHOBOS ROOM");centerText(160,72,0.34f,WHITE,roomLines[roomLine%ROOM_LINE_COUNT]);centerText(160,125,0.38f,DIM,"A: next line     X: pose");centerText(160,157,0.36f,DIM,"Выхода в меню здесь нет.");centerText(160,185,0.34f,DIM,"Только закрытие программы.");}
static void drawVideo(){videoFrame.drawCover(0,0,400,240,0.30f);C2D_TargetClear(botTarget,C2D_Color32(0,0,0,255));beginBottom();}
static void drawVtd(){vtdObs.drawFit(0,0,400,240,0.40f);C2D_TargetClear(botTarget,C2D_Color32(10,5,20,255));beginBottom();}
static void drawPornGallery(){C2D_TargetClear(topTarget,C2D_Color32(0,0,0,255));pornArts[pornImage%2].drawCover(0,0,400,240,0.30f);C2D_TargetClear(botTarget,C2D_Color32(10,3,14,255));beginBottom();if(phobosEnabled)phobosMenu.drawFit(84,8,152,224,0.55f);}
static void drawJetix(){jetixLogo.drawFit(95,25,210,165,0.45f);C2D_TargetClear(botTarget,C2D_Color32(0,0,0,255));beginBottom();}
static void drawCard(){Art& logo=secretTimer==0?chatgptLogo:sunoLogo;C2D_DrawRectSolid(35,20,0.1f,330,190,secretTimer==0?C2D_Color32(20,30,32,255):C2D_Color32(55,28,105,255));C2D_DrawRectSolid(42,27,0.2f,316,176,C2D_Color32(245,245,245,255));logo.drawFit(135,42,130,105,0.3f);centerText(200,166,0.65f,C2D_Color32(20,20,25,255),secretTimer==0?"CHATGPT":"SUNO");C2D_TargetClear(botTarget,BG);beginBottom();centerText(160,68,0.48f,WHITE,"Offline 3DS card");centerText(160,115,0.38f,DIM,"No browser is opened");centerText(160,165,0.4f,ACCENT,"A / B: return");}

static void drawMode(){switch(mode){case BOOT:drawBoot();break;case MENU:drawMenu();break;case RECORDS:drawRecords();break;case GALLERY:drawGallery();break;case SETTINGS:drawSettings();break;case GAME:drawGame();break;case CUTSCENE:drawCutscene();break;case WINNER:drawWinner();break;case ROUTE_VICTORY:drawRouteVictory();break;case ENDING:drawEnding();break;case ROOM_ENTRY:drawRoomEntry();break;case PHOBOS_ROOM:drawRoom();break;case VIDEO_MODE:drawVideo();break;case VTD_MODE:drawVtd();break;case PORN_GALLERY:drawPornGallery();break;case JETIX_MODE:drawJetix();break;case CARD_MODE:drawCard();break;}}
static void render(){
 C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
 stereoSlider=osGet3DSliderState();float separation=stereoSlider*0.54f;
 topTarget=topLeftTarget;eyeShift=-separation;C2D_TargetClear(topTarget,BG);beginTop();drawMode();
 topTarget=topRightTarget;eyeShift=separation;C2D_TargetClear(topTarget,BG);beginTop();drawMode();
 eyeShift=0.0f;topTarget=topLeftTarget;C3D_FrameEnd(0);
}

static void finishCutscene(){music.autoAdvance=true;if(cutsceneStage==100&&cutsceneReturn==GAME){mode=GAME;music.start(2);}else if(cutsceneStage==200&&cutsceneReturn==WINNER){mode=WINNER;winnerChoice=0;music.start(0);audio.play("romfs:/audio/music_winner_choice.mp3",true);}else{mode=cutsceneReturn;if(mode==MENU)music.start(0);}}
static int cutscenePages(){return 4;}
static void maybePauseReaction(bool leaving){if(!phobosEnabled||guardiansRoute||gameplayVtdTimer>0||voiceAudio.playing)return;int chance=leaving?34:52;if(rand()%100>=chance)return;char p[80];snprintf(p,sizeof(p),"romfs:/audio/react_phobos_pause_%d.mp3",rand()%6);playPhobosVoice(p,true);}
static void activatePause(){if(pauseIndex==0){paused=false;audio.setPause(false);sfxAudio.setPause(false);maybePauseReaction(true);}else if(pauseIndex==1){saveRecord();newGame();}else{saveRecord();paused=false;mode=MENU;music.start(0);}}
static void activateGameOver(){if(gameOverIndex==0)newGame();else{gameOver=false;mode=MENU;music.start(0);}}

static void handleInput(u32 kd,u32 kh,touchPosition tp){
 if(mode==BOOT){if(kd){startCutscene(0,MENU);}return;}
 if(mode==MENU){if(kd&KEY_UP)menuIndex=(menuIndex+4)%5;if(kd&KEY_DOWN)menuIndex=(menuIndex+1)%5;if(kd&KEY_A){if(menuIndex==0)newGame();else if(menuIndex==1){mode=RECORDS;recordsIndex=0;recordsConfirmReset=false;}else if(menuIndex==2){mode=GALLERY;galleryIndex=0;}else if(menuIndex==3){mode=SETTINGS;settingsIndex=0;}else running=false;}if(kd&KEY_START)running=false;return;}
 if(mode==RECORDS){if(recordsConfirmReset){if(kd&KEY_LEFT)recordsIndex=0;if(kd&KEY_RIGHT)recordsIndex=1;if(kd&KEY_A){if(recordsIndex==0)resetRecords();else{recordsConfirmReset=false;recordsIndex=0;}}if(kd&KEY_B){recordsConfirmReset=false;recordsIndex=0;}}else{if(kd&(KEY_UP|KEY_DOWN))recordsIndex^=1;if(kd&KEY_A){if(recordsIndex==0){recordsConfirmReset=true;recordsIndex=1;}else mode=MENU;}if(kd&KEY_B)mode=MENU;}return;}
 if(mode==GALLERY){if(kd&KEY_UP)galleryIndex=(galleryIndex+4)%5;if(kd&KEY_DOWN)galleryIndex=(galleryIndex+1)%5;if(kd&KEY_A){if(galleryIndex==0)startCutscene(0,GALLERY);else if(galleryIndex==1)startCutscene(100,GALLERY);else if(galleryIndex==2)startCutscene(200,GALLERY);else if(galleryIndex==3)startEnding(GALLERY);else mode=MENU;}if(kd&KEY_B)mode=MENU;return;}
 if(mode==SETTINGS){int n=settingsRoster?9:5;if(kd&KEY_UP)settingsIndex=(settingsIndex+n-1)%n;if(kd&KEY_DOWN)settingsIndex=(settingsIndex+1)%n;if(kd&(KEY_LEFT|KEY_RIGHT|KEY_A)){if(!settingsRoster){if(settingsIndex==0)dualScreen=!dualScreen;else if(settingsIndex==1){phobosFall=!phobosFall;resetRandomizer();resetClassicLock();}else if(settingsIndex==2){int delta=(kd&KEY_LEFT)?-1:1;startSpeed+=delta;if(startSpeed<1)startSpeed=5;if(startSpeed>5)startSpeed=1;}else if(settingsIndex==3){settingsRoster=true;settingsIndex=0;return;}else{mode=MENU;return;}}else{if(settingsIndex<7){pieceEnabled[settingsIndex]=!pieceEnabled[settingsIndex];resetRandomizer();}else if(settingsIndex==7)phobosEnabled=!phobosEnabled;else{settingsRoster=false;settingsIndex=0;return;}}saveSettings();}if(kd&KEY_B){if(settingsRoster){settingsRoster=false;settingsIndex=0;}else mode=MENU;}return;}
 if(mode==GAME){
  if(phobosDeletedWin){if(kd){phobosDeletedWin=false;mode=MENU;music.autoAdvance=true;music.start(0);}return;}
  if(gameOver){if(kd&KEY_UP||kd&KEY_DOWN)gameOverIndex^=1;if(kd&KEY_A)activateGameOver();if(kd&KEY_B){gameOverIndex=1;activateGameOver();}return;}
  if(paused){if(kd&KEY_UP)pauseIndex=(pauseIndex+2)%3;if(kd&KEY_DOWN)pauseIndex=(pauseIndex+1)%3;if(kd&(KEY_A|KEY_Y|KEY_RIGHT))activatePause();if(kd&(KEY_B|KEY_START|KEY_SELECT)){paused=false;audio.setPause(false);sfxAudio.setPause(false);maybePauseReaction(true);}return;}
  if(kd&(KEY_START|KEY_SELECT)){paused=true;pauseIndex=0;audio.setPause(true);sfxAudio.setPause(true);maybePauseReaction(false);return;}
  if(clearPending)return;
  bool ax=((kh&(KEY_A|KEY_X))==(KEY_A|KEY_X))&&(kd&(KEY_A|KEY_X));if(ax){developerAddLines();return;}
  bool ab=((kh&(KEY_A|KEY_B))==(KEY_A|KEY_B))&&(kd&(KEY_A|KEY_B));if(ab){dualScreen=!dualScreen;saveSettings();if(!layoutReactionDone&&rand()%100<8&&playPhobosVoice("romfs:/audio/react_phobos_layout.mp3")){layoutReactionDone=true;}return;}
  if(kd&KEY_TOUCH){bool hit=dualScreen?(tp.px<=40&&tp.py>=190):(tp.px>=178&&tp.px<=304&&tp.py>=188);if(hit){openCodeKeyboard();return;}}
  if(kd&KEY_LEFT)moveHorizontal(-1);if(kd&KEY_RIGHT)moveHorizontal(1);
  if(!phobosFall){int direction=((kh&KEY_RIGHT)?1:0)-((kh&KEY_LEFT)?1:0);if(direction!=dasDirection){dasDirection=direction;dasFrames=0;}if(direction){dasFrames++;if(dasFrames>=10&&(dasFrames-10)%3==0)moveHorizontal(direction);}}
  if(kh&KEY_DOWN&&frameCounter%3==0&&curType>=0){if(fits(curType,curRot,curX,curY+1)){curY++;if(!phobosFall)lockFrames=0;}else if(phobosFall)lockPiece();}
  if(kd&(KEY_A|KEY_UP))rotatePiece(1);if(kd&KEY_B)rotatePiece(-1);if(kd&KEY_Y)hardDrop();if(kd&KEY_X)hold();if(kd&(KEY_L|KEY_R)){if(gameplayVtdTimer>0){gameplayVtdTimer=0;music.autoAdvance=true;}music.playNext();}return;
 }
 if(mode==CUTSCENE){
  if(kd&(KEY_B|KEY_START)){cutscenePage=cutscenePages();finishCutscene();return;}
  if(kd&(KEY_A|KEY_TOUCH)){
   if(cutsceneStage==100&&cutscenePage==0){if(story100Tick<78)story100Tick=78;else if(story100Tick<120)story100Tick=120;else if(story100Tick<440)story100Tick=440;else{cutscenePage=1;story100Tick=0;music.autoAdvance=false;audio.play("romfs:/audio/music_cutscene_lines100.mp3");}}
   else{cutscenePage++;if(cutsceneStage==100&&cutscenePage==3){voiceAudio.stop();voiceAudio.play((rand()&1)?"romfs:/audio/story100_phobos_pay_short.mp3":"romfs:/audio/story100_phobos_pay_full.mp3");}if(cutscenePage>=cutscenePages())finishCutscene();}
  }
  return;
 }
 if(mode==WINNER){if(kd&KEY_LEFT)winnerChoice=0;if(kd&KEY_RIGHT)winnerChoice=1;if(kd&KEY_A)chooseWinner();if(kd&KEY_Y)keyboardRussian=!keyboardRussian;if(kd&KEY_X)popUtf8(codeBuffer);if(kd&KEY_START){std::string entered=codeBuffer;codeBuffer.clear();if(!entered.empty())handleCode(entered);}if(kd&KEY_TOUCH)handleVirtualKeyboardTouch(tp.px,tp.py);return;}
 if(mode==ROUTE_VICTORY){if(victoryTimer>30&&!voiceAudio.playing&&kd&(KEY_A|KEY_B|KEY_START|KEY_TOUCH))continueRouteVictory();return;}
 if(mode==ENDING){if(endingFinished&&kd&(KEY_A|KEY_B|KEY_TOUCH|KEY_START)){endingFrame.free();mode=(returnMode==GALLERY)?GALLERY:MENU;music.autoAdvance=true;music.start(0);}return;}
 if(mode==ROOM_ENTRY)return;
 if(mode==PHOBOS_ROOM){if(kd&KEY_X)roomPose=(roomPose+1)%6;if(kd&KEY_A){roomPose=(roomPose+1)%6;roomLine=(roomLine+1+rand()%3)%ROOM_LINE_COUNT;}return;}
 if(mode==VIDEO_MODE){if(kd&(KEY_A|KEY_B|KEY_START)){if(videoKind==1)running=false;else{videoFrame.free();mode=returnMode;if(mode==WINNER)resumeWinnerMusic();else{music.autoAdvance=true;music.playNext();}}}return;}
 if(mode==VTD_MODE){if(((kh&KEY_L)&&(kh&KEY_R))||(kd&KEY_START))running=false;return;}
 if(mode==PORN_GALLERY||mode==JETIX_MODE||mode==CARD_MODE){if(kd&(KEY_A|KEY_B|KEY_START)){mode=returnMode;if(mode==WINNER)resumeWinnerMusic();else{music.autoAdvance=true;music.playNext();}}return;}
}

static void updateVideo(){int idx=(videoTick*videoFps)/60;if(idx>=videoCount||audio.finished){if(videoKind==1)running=false;else{videoFrame.free();mode=returnMode;if(mode==WINNER)resumeWinnerMusic();else{music.autoAdvance=true;music.playNext();}}return;}if(idx!=videoLoaded){videoLoaded=idx;videoFrame.free();char p[96];snprintf(p,sizeof(p),videoKind==1?"romfs:/video/matrix/matrix_%03d.t3x":"romfs:/video/porn/porn_%03d.t3x",idx);videoFrame.load(p);}videoTick++;}
static void updateEnding(){if(endingFinished)return;u64 elapsed=osGetTime()-endingStartMs;int idx=(int)((elapsed*ENDING_FPS)/1000);if(idx>=ENDING_FRAME_COUNT){endingFinished=true;audio.stop();return;}if(idx!=endingLoaded){endingLoaded=idx;endingFrame.free();char p[96];snprintf(p,sizeof(p),"romfs:/video/ending/ending_%03d.t3x",idx);endingFrame.load(p);}endingTick++;}
static void update(){
 frameCounter++;music.update();voiceAudio.update();sfxAudio.update();
 bool duckNow=mode==GAME&&voiceAudio.playing;if(duckNow!=musicDucked){musicDucked=duckNow;audio.setVolume(duckNow?0.24f:0.72f);}
 if(voiceCooldown>0)voiceCooldown--;
 if(mode==CUTSCENE&&cutsceneStage==100&&cutscenePage==0){story100Tick++;if(story100Tick<56)audio.setPause(((story100Tick/5)%3)==1);else if(!story100AudioStopped){audio.setPause(false);audio.stop();story100AudioStopped=true;}}
 if(clearFxTimer>0){clearFxTimer--;if(clearFxTimer==0&&clearPending)finishPendingClear();}if(gameplayMatrixTimer>0)gameplayMatrixTimer--;if(gameplayJetixTimer>0)gameplayJetixTimer--;
 if(gameplayVtdTimer>0){gameplayVtdTimer--;if(audio.finished||gameplayVtdTimer==0){audio.finished=false;gameplayVtdTimer=0;music.autoAdvance=true;music.playNext();}}
 if(mode==VIDEO_MODE)updateVideo();if(mode==ENDING)updateEnding();if(mode==ROUTE_VICTORY)victoryTimer++;
 if(mode==ROOM_ENTRY){roomEntryTimer++;if((roomEntryTimer>10&&!voiceAudio.playing)||roomEntryTimer>60*12)finishRoomEntry();}
 if(mode==VTD_MODE&&audio.finished)running=false;if(mode==JETIX_MODE&&secretTimer>0){secretTimer--;if(!secretTimer){mode=returnMode;if(mode==WINNER)resumeWinnerMusic();else{music.autoAdvance=true;music.playNext();}}}
 if(mode==GAME&&!paused&&!gameOver&&!phobosDeletedWin&&curType<0){bool anyPiece=false;for(int i=0;i<7;i++)anyPiece|=pieceEnabled[i];if(!anyPiece){emptyRosterFrames++;if(!emptyRosterBored&&emptyRosterFrames>=600){emptyRosterBored=true;boredFrames=0;audio.setPause(true);}else if(emptyRosterBored&&++boredFrames>=156){gameOver=true;gameOverIndex=0;saveRecord();}}}
 if(mode==GAME&&!paused&&!gameOver&&!phobosDeletedWin&&!clearPending){playFrames++;if(!pauseHintPlayed&&playFrames>=pauseHintEligibleAt&&playFrames%(60*10)==0&&rand()%100<18){if(playPhobosVoice("romfs:/audio/react_phobos_pause_hint.mp3"))pauseHintPlayed=true;}}
 if(mode==GAME&&!paused&&!gameOver&&!clearPending&&curType>=0){
  if(!phobosFall){if(!fits(curType,curRot,curX,curY+1)){lockFrames++;if(lockFrames>=30){lockPiece();return;}}else lockFrames=0;}
  gravityFrames++;if(gravityFrames>=gravityInterval()){gravityFrames=0;if(fits(curType,curRot,curX,curY+1)){curY++;if(!phobosFall)lockFrames=0;}else if(phobosFall)lockPiece();}
 }
}

static void loadArt(){
 bgMenu.load("romfs:/gfx/bg_menu.t3x");bgGame[0].load("romfs:/gfx/bg_phase0.t3x");bgGame[1].load("romfs:/gfx/bg_phase1.t3x");bgGame[2].load("romfs:/gfx/bg_phase2.t3x");bgDual[0].load("romfs:/gfx/bg_phase0_dual.t3x");bgDual[1].load("romfs:/gfx/bg_phase1_dual.t3x");bgDual[2].load("romfs:/gfx/bg_phase2_dual.t3x");phobosMenu.load("romfs:/gfx/phobos_menu_body.t3x");phobosGame.load("romfs:/gfx/phobos_gameplay.t3x");phaseCells.load("romfs:/gfx/phase1_cells.t3x");horrorCells.load("romfs:/gfx/horror_cells.t3x");vtdObs.load("romfs:/gfx/vtd_observer.t3x");roomBg.load("romfs:/gfx/phobos_room_bg.t3x");roomFg.load("romfs:/gfx/phobos_room_foreground.t3x");
 for(int i=0;i<6;i++){char p[80];snprintf(p,sizeof(p),"romfs:/gfx/phobos_room_pose%d.t3x",i);roomPoses[i].load(p);}introCastle.load("romfs:/gfx/intro_castle.t3x");introHall.load("romfs:/gfx/intro_throne.t3x");introPhobos.load("romfs:/gfx/intro_phobos.t3x");
 for(int i=0;i<7;i++){char p[96];snprintf(p,sizeof(p),"romfs:/gfx/intro_%s.t3x",charFiles[i]);introNormal[i].load(p);snprintf(p,sizeof(p),"romfs:/gfx/intro_%s_final.t3x",charFiles[i]);introFinal[i].load(p);snprintf(p,sizeof(p),"romfs:/gfx/ending_%s.t3x",charFiles[i]);endingArt[i].load(p);}
 l100Will.load("romfs:/gfx/l100_will.t3x");l100Phobos.load("romfs:/gfx/l100_phobos.t3x");l100Heart.load("romfs:/gfx/l100_heart.t3x");terminalHeartMask.load("romfs:/gfx/terminal_heart_mask.t3x");terminalJetixMask.load("romfs:/gfx/terminal_jetix_mask.t3x");endingHeart.load("romfs:/gfx/ending_heart.t3x");endingWitch.load("romfs:/gfx/ending_witch.t3x");pornArts[0].load("romfs:/gfx/secret_porn0.t3x");pornArts[1].load("romfs:/gfx/secret_porn1.t3x");jetixLogo.load("romfs:/gfx/jetix_logo.t3x");chatgptLogo.load("romfs:/gfx/logo_chatgpt.t3x");sunoLogo.load("romfs:/gfx/logo_suno.t3x");
}
static void freeArt(){bgMenu.free();for(auto& a:bgGame)a.free();for(auto& a:bgDual)a.free();phobosMenu.free();phobosGame.free();phaseCells.free();horrorCells.free();vtdObs.free();roomBg.free();roomFg.free();for(auto& a:roomPoses)a.free();introCastle.free();introHall.free();introPhobos.free();for(auto& a:introNormal)a.free();for(auto& a:introFinal)a.free();for(auto& a:endingArt)a.free();l100Will.free();l100Phobos.free();l100Heart.free();terminalHeartMask.free();terminalJetixMask.free();endingHeart.free();endingWitch.free();for(auto& a:pornArts)a.free();jetixLogo.free();chatgptLogo.free();sunoLogo.free();videoFrame.free();endingFrame.free();}

int main(){srand((unsigned)time(nullptr));gfxInitDefault();gfxSet3D(true);romfsInit();cfguInit();C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);C2D_Init(C2D_DEFAULT_MAX_OBJECTS);C2D_Prepare();topLeftTarget=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);topRightTarget=C2D_CreateScreenTarget(GFX_TOP,GFX_RIGHT);topTarget=topLeftTarget;botTarget=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);textBuf=C2D_TextBufNew(4096);sysFont=C2D_FontLoadSystem(CFG_REGION_EUR);loadSettings();loadRecords();loadArt();audio.init(true);voiceAudio.init(false);sfxAudio.init(false);audio.setVolume(0.72f);voiceAudio.setVolume(1.0f);sfxAudio.setVolume(0.86f);while(aptMainLoop()&&running){hidScanInput();u32 kd=hidKeysDown(),kh=hidKeysHeld();touchPosition tp;hidTouchRead(&tp);handleInput(kd,kh,tp);update();render();}sfxAudio.fini(false);voiceAudio.fini(false);audio.fini(true);freeArt();C2D_FontFree(sysFont);C2D_TextBufDelete(textBuf);C2D_Fini();C3D_Fini();cfguExit();romfsExit();gfxExit();return 0;}
