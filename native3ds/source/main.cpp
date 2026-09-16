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
static constexpr u32 BG=C2D_Color32(8,5,18,255);
static constexpr u32 WHITE=C2D_Color32(245,240,255,255);
static constexpr u32 ACCENT=C2D_Color32(218,167,255,255);
static constexpr u32 DIM=C2D_Color32(150,140,170,255);
static constexpr u32 RED=C2D_Color32(255,90,110,255);
static constexpr u32 GREEN=C2D_Color32(70,255,125,255);

static C3D_RenderTarget *topTarget=nullptr,*botTarget=nullptr;
static C2D_TextBuf textBuf=nullptr;
static C2D_Font sysFont=nullptr;

static void drawText(float x,float y,float scale,u32 color,const char* fmt,...){
    char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    C2D_Text t; C2D_TextBufClear(textBuf); C2D_TextFontParse(&t,sysFont,textBuf,buf); C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_WithColor,x,y,0.8f,scale,scale,color);
}
static void centerText(float cx,float y,float scale,u32 color,const char* s){
    C2D_Text t; C2D_TextBufClear(textBuf); C2D_TextFontParse(&t,sysFont,textBuf,s); C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_WithColor,cx-t.width*scale*0.5f,y,0.8f,scale,scale,color);
}

struct Art {
    C2D_SpriteSheet sheet=nullptr;
    bool load(const char* path){ sheet=C2D_SpriteSheetLoad(path); return sheet!=nullptr; }
    void free(){ if(sheet){ C2D_SpriteSheetFree(sheet); sheet=nullptr; } }
    void drawFit(float x,float y,float w,float h,float z=0.1f,float alpha=1.0f) const {
        if(!sheet) return; C2D_Image im=C2D_SpriteSheetGetImage(sheet,0);
        float iw=(float)im.subtex->width, ih=(float)im.subtex->height;
        float s=std::min(w/iw,h/ih); float dx=x+(w-iw*s)/2, dy=y+(h-ih*s)/2;
        // A white tint with blend factor 1.0 replaces the source RGB and turns
        // every character into a white silhouette.  Most art is fully opaque,
        // so draw it without a tint and preserve the original palette.
        (void)alpha;
        C2D_DrawImageAt(im,dx,dy,z,nullptr,s,s);
    }
    void drawCover(float x,float y,float w,float h,float z=0.1f) const {
        if(!sheet) return; C2D_Image im=C2D_SpriteSheetGetImage(sheet,0);
        float iw=(float)im.subtex->width, ih=(float)im.subtex->height;
        float s=std::max(w/iw,h/ih); float dx=x+(w-iw*s)/2, dy=y+(h-ih*s)/2;
        C2D_DrawImageAt(im,dx,dy,z,nullptr,s,s);
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
        C2D_DrawImageAt(im,x,y,z,nullptr,sx,sy);
    }
};

struct Audio {
    static constexpr int CH=0, BUF_SAMPLES=4096, BUF_SIZE=BUF_SAMPLES*2*2;
    mpg123_handle* mh=nullptr; ndspWaveBuf wb[2]{}; u8* mem=nullptr;
    bool ready=false, playing=false, paused=false, finished=false, looping=false;
    long rate=44100; std::string path;
    bool init(){
        if(R_FAILED(ndspInit())) return false;
        if(mpg123_init()!=MPG123_OK){ ndspExit(); return false; }
        mem=(u8*)linearAlloc(BUF_SIZE*2); ready=mem!=nullptr; return ready;
    }
    void stop(){
        if(!ready) return; ndspChnReset(CH); if(mh){ mpg123_close(mh); mpg123_delete(mh); mh=nullptr; }
        playing=paused=finished=false;
    }
    size_t fill(u8* b){
        size_t total=0;
        while(total<BUF_SIZE){ size_t done=0; int r=mpg123_read(mh,b+total,BUF_SIZE-total,&done); total+=done; if(r==MPG123_DONE||r==MPG123_ERR||done==0) break; }
        return total;
    }
    bool play(const char* p,bool loop=false){
        if(!ready) return false; stop(); int err=0; mh=mpg123_new(nullptr,&err); if(!mh) return false;
        mpg123_param(mh,MPG123_ADD_FLAGS,MPG123_FORCE_STEREO,0);
        if(mpg123_open(mh,p)!=MPG123_OK){ mpg123_delete(mh); mh=nullptr; return false; }
        int ch=0,enc=0; if(mpg123_getformat(mh,&rate,&ch,&enc)!=MPG123_OK){ stop(); return false; }
        mpg123_format_none(mh); mpg123_format(mh,rate,MPG123_STEREO,MPG123_ENC_SIGNED_16);
        ndspChnReset(CH); ndspChnSetInterp(CH,NDSP_INTERP_LINEAR); ndspChnSetRate(CH,(float)rate); ndspChnSetFormat(CH,NDSP_FORMAT_STEREO_PCM16);
        float mix[12]={1,1}; ndspChnSetMix(CH,mix);
        memset(wb,0,sizeof(wb));
        for(int i=0;i<2;i++){ u8* b=mem+i*BUF_SIZE; size_t n=fill(b); if(!n) break; DSP_FlushDataCache(b,BUF_SIZE); wb[i].data_vaddr=b; wb[i].nsamples=n/4; ndspChnWaveBufAdd(CH,&wb[i]); }
        path=p; looping=loop; playing=true; paused=false; finished=false; return true;
    }
    void setPause(bool p){ if(!playing) return; paused=p; ndspChnSetPaused(CH,p); }
    void update(){
        if(!playing||paused||!mh) return;
        for(int i=0;i<2;i++) if(wb[i].status==NDSP_WBUF_DONE){ u8* b=mem+i*BUF_SIZE; size_t n=fill(b); if(!n){
            if(looping){ std::string cp=path; play(cp.c_str(),true); } else { stop(); finished=true; } return;
        } DSP_FlushDataCache(b,BUF_SIZE); wb[i].nsamples=n/4; wb[i].status=NDSP_WBUF_FREE; ndspChnWaveBufAdd(CH,&wb[i]); }
    }
    void fini(){ stop(); if(mem) linearFree(mem); mem=nullptr; if(ready){ mpg123_exit(); ndspExit(); } ready=false; }
} audio;

struct Music {
    std::vector<std::string> pool, bag; std::string fixedFirst,last; bool firstPending=true, firstCycle=true; int phase=-1;
    bool autoAdvance=true;
    void configure(int p){
        if(phase==p) return; phase=p; pool.clear(); bag.clear(); firstPending=true; firstCycle=true; last.clear();
        if(p==0){ fixedFirst="romfs:/audio/menu_1.mp3"; pool={"romfs:/audio/menu_1.mp3","romfs:/audio/menu_2.mp3"}; }
        else if(p==1){ fixedFirst="romfs:/audio/phase0.mp3"; pool={"romfs:/audio/phase0.mp3","romfs:/audio/music_arcade_1.mp3","romfs:/audio/music_arcade_3.mp3","romfs:/audio/music_arcade_6.mp3"}; }
        else if(p==2){ fixedFirst="romfs:/audio/phase1_1.mp3"; pool={"romfs:/audio/phase1_1.mp3","romfs:/audio/phase1_2.mp3","romfs:/audio/phase1_3.mp3","romfs:/audio/music_phase1_hollow.mp3"}; }
        else if(p==3){ fixedFirst="romfs:/audio/phase2_guardians.mp3"; pool={"romfs:/audio/phase2_guardians.mp3","romfs:/audio/music_arcade_2.mp3","romfs:/audio/music_arcade_4.mp3","romfs:/audio/music_arcade_5.mp3"}; }
        else { fixedFirst="romfs:/audio/phase2_phobos.mp3"; pool={"romfs:/audio/phase2_phobos.mp3","romfs:/audio/music_empty_hollow_1.mp3","romfs:/audio/music_empty_hollow_2.mp3","romfs:/audio/music_crucified.mp3","romfs:/audio/music_crusified2.mp3"}; }
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
        if(firstPending){ firstPending=false; last=fixedFirst; return last; }
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

enum Mode { MENU, GALLERY, SETTINGS, GAME, CUTSCENE, WINNER, ENDING, PHOBOS_ROOM,
            VIDEO_MODE, VTD_MODE, PORN_GALLERY, JETIX_MODE, CARD_MODE };
static Mode mode=CUTSCENE, returnMode=MENU, cutsceneReturn=MENU;
static bool running=true, paused=false, gameOver=false, dualScreen=false, phobosFall=true;
static bool phobosRoute=false, guardiansRoute=false, horrorPieces=false;
static int menuIndex=0,galleryIndex=0,settingsIndex=0,pauseIndex=0,gameOverIndex=0,winnerChoice=0;
static int cutsceneStage=0,cutscenePage=0,roomPose=0,roomLine=0,pornImage=0,endingPage=0;
static int score=0,lines=0,level=1,frameCounter=0,secretTimer=0;
static int board[BH][BW]{};
static int curType=0,curRot=0,curX=3,curY=-1,nextType=1,holdType=-1;
static bool holdUsed=false;
static int bag[7],bagPos=7;
static std::string codeMessage;

static Art bgMenu,bgGame[3],phobosMenu,phobosGame,vtdObs,roomBg,roomFg,roomPoses[6];
static Art introCastle,introPhobos,introNormal[7],introFinal[7],l100Will,l100Phobos,endingHeart,endingArt[7];
static Art pornArts[2],jetixLogo,chatgptLogo,sunoLogo,videoFrame;
static Sheet phaseCells,horrorCells;
static int videoKind=0,videoTick=0,videoLoaded=-1,videoCount=0,videoFps=0;

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

static void saveSettings(){ mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/WitchTetris",0777);FILE* f=fopen("sdmc:/3ds/WitchTetris/settings.cfg","w");if(f){fprintf(f,"dual=%d\nphobosfall=%d\n",dualScreen?1:0,phobosFall?1:0);fclose(f);} }
static void loadSettings(){FILE* f=fopen("sdmc:/3ds/WitchTetris/settings.cfg","r");if(!f)return;char k[64];int v;while(fscanf(f,"%63[^=]=%d\n",k,&v)==2){if(!strcmp(k,"dual"))dualScreen=v;if(!strcmp(k,"phobosfall"))phobosFall=v;}fclose(f);}

static const int baseShape[7][4][2]={
 {{0,1},{1,1},{2,1},{3,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{0,1},{1,1},{2,1}},
 {{1,0},{2,0},{0,1},{1,1}},{{0,0},{1,0},{1,1},{2,1}},{{0,0},{0,1},{1,1},{2,1}},{{2,0},{0,1},{1,1},{2,1}}
};
static void blockPos(int t,int r,int i,int& x,int& y){x=baseShape[t][i][0];y=baseShape[t][i][1];if(t==1)return;for(int k=0;k<r;k++){int nx=3-y,ny=x;x=nx;y=ny;}}
static bool fits(int t,int r,int px,int py){for(int i=0;i<4;i++){int x,y;blockPos(t,r,i,x,y);x+=px;y+=py;if(x<0||x>=BW||y>=BH)return false;if(y>=0&&board[y][x])return false;}return true;}
static int nextBag(){if(bagPos>=7){for(int i=0;i<7;i++)bag[i]=i;for(int i=6;i>0;i--){int j=rand()%(i+1);std::swap(bag[i],bag[j]);}bagPos=0;}return bag[bagPos++];}
static int nextPiece(){if(!phobosFall)return nextBag();if(rand()%100<45)return rand()%7;return nextBag();}

// The supplied atlas order is I,O,T,S,Z,J,L; S/Z are opposite to baseShape.
static int spriteIndex(int t,int r,int i){
 int at=t,sr=r,ci=i;
 if(t==0)sr=(r+3)&3;
 else if(t==1){static const int om[4][4]={{0,1,2,3},{2,0,3,1},{3,2,1,0},{1,3,0,2}};ci=om[r&3][i];}
 else if(t==2){sr=(r+2)&3;ci=3-i;}
 else if(t==3)at=4;
 else if(t==4)at=3;
 return at*16+sr*4+ci;
}
static int encodeCell(int t,int r,int i,bool horror){return ((t+1)<<8)|(spriteIndex(t,r,i)+1)|(horror?0x10000:0);}
static int cellType(int code){return code<0?(-code-1):(((code>>8)&255)-1);}
static u32 pieceColor(int t){static u32 c[7]={C2D_Color32(80,220,255,255),C2D_Color32(255,220,70,255),C2D_Color32(190,90,255,255),C2D_Color32(80,240,130,255),C2D_Color32(255,80,100,255),C2D_Color32(80,110,255,255),C2D_Color32(255,145,60,255)};return c[(t<0?0:t)%7];}
static bool plainMode(){return guardiansRoute||(phobosRoute&&!horrorPieces);}

static void startCutscene(int stage,Mode after){mode=CUTSCENE;cutsceneStage=stage;cutscenePage=0;cutsceneReturn=after;music.autoAdvance=false;if(stage==0)audio.play("romfs:/audio/intro.mp3");else if(stage==100)audio.play("romfs:/audio/music_cutscene_lines100.mp3");else audio.play("romfs:/audio/music_cutscene_lines200.mp3");}
static void enterRoom(){mode=PHOBOS_ROOM;roomPose=rand()%6;roomLine=rand()%ROOM_LINE_COUNT;music.autoAdvance=false;audio.play("romfs:/audio/phobos_room.mp3",true);}
static void spawn(){curType=nextType;nextType=nextPiece();curRot=0;curX=3;curY=-1;holdUsed=false;if(!fits(curType,curRot,curX,curY)){if(phobosRoute)enterRoom();else if(guardiansRoute){mode=ENDING;returnMode=MENU;endingPage=0;music.autoAdvance=false;audio.play("romfs:/audio/ending_outro.mp3");}else{gameOver=true;gameOverIndex=0;}}}
static void newGame(){memset(board,0,sizeof(board));score=lines=0;level=1;gameOver=paused=false;holdType=-1;holdUsed=false;bagPos=7;phobosRoute=guardiansRoute=horrorPieces=false;codeMessage.clear();nextType=nextPiece();mode=GAME;spawn();music.autoAdvance=true;music.start(1);}

static void triggerMilestones(int before){
 if(before<100&&lines>=100){startCutscene(100,GAME);return;}
 if(before<200&&lines>=200){startCutscene(200,WINNER);return;}
 if(phobosRoute&&before<300&&lines>=300){enterRoom();return;}
 int phase=lines<100?1:lines<200?2:guardiansRoute?3:4;
 if(music.phase!=phase)music.start(phase);
}
static void developerAddLines(){int before=lines;lines+=10;score+=1000;codeMessage="DEBUG: +10 LINES";triggerMilestones(before);}
static void clearLines(){int cleared=0;for(int y=BH-1;y>=0;y--){bool full=true;for(int x=0;x<BW;x++)if(!board[y][x]){full=false;break;}if(full){cleared++;for(int yy=y;yy>0;yy--)memcpy(board[yy],board[yy-1],sizeof(board[0]));memset(board[0],0,sizeof(board[0]));y++;}}if(cleared){int before=lines;lines+=cleared;score+=100*cleared*cleared;level=1+lines/10;triggerMilestones(before);}}
static void lockPiece(){bool horror=phobosRoute&&horrorPieces;for(int i=0;i<4;i++){int x,y;blockPos(curType,curRot,i,x,y);x+=curX;y+=curY;if(y>=0&&y<BH&&x>=0&&x<BW)board[y][x]=plainMode()?-(curType+1):encodeCell(curType,curRot,i,horror);}clearLines();if(mode==GAME)spawn();}
static void hardDrop(){while(fits(curType,curRot,curX,curY+1)){curY++;score+=2;}lockPiece();}
static void hold(){if(holdUsed)return;if(holdType<0){holdType=curType;spawn();}else{std::swap(holdType,curType);curRot=0;curX=3;curY=-1;}holdUsed=true;}

static void drawPlain(float x,float y,float cell,int t,float z){u32 col=pieceColor(t);C2D_DrawRectSolid(x+1,y+1,z,cell-2,cell-2,col);C2D_DrawRectSolid(x+2,y+2,z+0.01f,cell-4,std::max(1.0f,cell*0.16f),C2D_Color32(255,255,255,115));}
static void drawSpriteCell(int idx,bool horror,float x,float y,float cell,float z){(horror?horrorCells:phaseCells).draw(idx,x,y,cell,z);}
static void drawBoardSlice(float x0,float y0,float cell,int yStart,int count){
 C2D_DrawRectSolid(x0-2,y0-2,0.2f,BW*cell+4,count*cell+4,C2D_Color32(90,55,120,255));C2D_DrawRectSolid(x0,y0,0.3f,BW*cell,count*cell,C2D_Color32(5,5,12,245));
 for(int yy=0;yy<count;yy++){int y=yStart+yy;for(int x=0;x<BW;x++){int code=board[y][x];if(!code)continue;if(code<0)drawPlain(x0+x*cell,y0+yy*cell,cell,cellType(code),0.5f);else drawSpriteCell((code&255)-1,(code&0x10000)!=0,x0+x*cell,y0+yy*cell,cell,0.5f);}}
 for(int i=0;i<4;i++){int x,y;blockPos(curType,curRot,i,x,y);x+=curX;y+=curY;if(y<yStart||y>=yStart+count)continue;float dx=x0+x*cell,dy=y0+(y-yStart)*cell;if(plainMode())drawPlain(dx,dy,cell,curType,0.6f);else drawSpriteCell(spriteIndex(curType,curRot,i),phobosRoute&&horrorPieces,dx,dy,cell,0.6f);}
}
static void drawMiniPiece(int t,float x,float y,float cell){int minx=9,miny=9;for(int i=0;i<4;i++){int bx,by;blockPos(t,0,i,bx,by);minx=std::min(minx,bx);miny=std::min(miny,by);}for(int i=0;i<4;i++){int bx,by;blockPos(t,0,i,bx,by);float dx=x+(bx-minx)*cell,dy=y+(by-miny)*cell;if(plainMode())drawPlain(dx,dy,cell,t,0.5f);else drawSpriteCell(spriteIndex(t,0,i),phobosRoute&&horrorPieces,dx,dy,cell,0.5f);}}

static std::string lowerAscii(std::string s){for(char& c:s)if((unsigned char)c<128)c=(char)std::tolower((unsigned char)c);return s;}
static bool any(const std::string& s,std::initializer_list<const char*> values){for(const char* v:values)if(s==v)return true;return false;}
static void startVideo(int kind,Mode after){videoKind=kind;videoTick=0;videoLoaded=-1;videoCount=kind==1?214:35;videoFps=kind==1?6:12;returnMode=after;videoFrame.free();mode=VIDEO_MODE;music.autoAdvance=false;audio.play(kind==1?"romfs:/video/matrix.mp3":"romfs:/video/porn.mp3");}
static void startVtd(){returnMode=mode;mode=VTD_MODE;music.autoAdvance=false;if(!audio.play((rand()%2)?"romfs:/audio/vtd_1.mp3":"romfs:/audio/vtd_2.mp3"))running=false;}
static void chooseWinner(){
 guardiansRoute=winnerChoice==0;phobosRoute=!guardiansRoute;horrorPieces=phobosRoute&&(rand()%100<80);
 if(guardiansRoute){for(int y=0;y<BH;y++)for(int x=0;x<BW;x++)if(board[y][x])board[y][x]=-(cellType(board[y][x])+1);}
 mode=GAME;music.autoAdvance=true;music.start(guardiansRoute?3:4);
}
static void handleCode(const std::string& raw){
 std::string s=lowerAscii(raw);codeMessage.clear();
 if(mode==GAME&&any(s,{"q","й"})){developerAddLines();return;}
 if(mode==GAME&&any(s,{"witch","vich","витч","вич","guardians","стражницы","чародейки","kandrakar","кондракар"})){memset(board,0,sizeof(board));codeMessage="THE BOARD IS CLEAR";return;}
 if(any(s,{"matrix","матрица"})){startVideo(1,mode);return;}
 if(any(s,{"vtd","втд","valentin","valentine","валентин"})){startVtd();return;}
 if(any(s,{"jetix","jtx","джетикс","джт"})){returnMode=mode;mode=JETIX_MODE;secretTimer=60*6;return;}
 if(any(s,{"gpt","chatgpt","гпт"})){returnMode=mode;mode=CARD_MODE;secretTimer=0;return;}
 if(any(s,{"suno","suna","суно","суна"})){returnMode=mode;mode=CARD_MODE;secretTimer=1;return;}
 if(any(s,{"porn","порн"})){
  if(mode==WINNER){startVideo(2,WINNER);return;}
  returnMode=mode;mode=PORN_GALLERY;pornImage=rand()%2;music.autoAdvance=false;audio.play("romfs:/audio/voice_porn.mp3");return;
 }
 if(mode==WINNER&&any(s,{"witch","vich","витч","вич","guardians","стражницы","чародейки"})){winnerChoice=0;chooseWinner();return;}
 if(any(s,{"phobos","fobos","фобос"})){
  if(mode==WINNER){winnerChoice=1;chooseWinner();return;}
  audio.play("romfs:/audio/voice_phobos.mp3");codeMessage="ФОБОС УСЛЫШАЛ ТЕБЯ";return;
 }
 codeMessage="UNKNOWN CODE";
}
static void openCodeKeyboard(){char buf[64]={0};SwkbdState sw;swkbdInit(&sw,SWKBD_TYPE_NORMAL,1,32);swkbdSetHintText(&sw,"Q / WITCH / PHOBOS / VTD / MATRIX / JTX / GPT / SUNO / PORN");swkbdSetButton(&sw,SWKBD_BUTTON_RIGHT,"OK",true);swkbdSetFeatures(&sw,SWKBD_DEFAULT_QWERTY|SWKBD_ALLOW_HOME);if(swkbdInputText(&sw,buf,sizeof(buf))!=SWKBD_BUTTON_NONE)handleCode(buf);}

static void drawMenu(){bgMenu.drawCover(0,0,TOP_W,H);C2D_DrawRectSolid(0,0,0.2f,TOP_W,H,C2D_Color32(0,0,0,90));phobosMenu.drawFit(250,18,145,216,0.3f);drawText(18,20,0.72f,ACCENT,"W.I.T.C.H. TETRIS 3DS");drawText(20,48,0.43f,WHITE,"NATIVE STORY TEST 2");const char* items[]={"NEW GAME","CUTSCENES","SETTINGS","EXIT"};for(int i=0;i<4;i++){if(i==menuIndex)C2D_DrawRectSolid(18,82+i*34,0.4f,205,28,C2D_Color32(95,45,120,220));drawText(28,85+i*34,0.55f,i==menuIndex?WHITE:DIM,"> %s",items[i]);}C2D_TargetClear(botTarget,BG);C2D_SceneBegin(botTarget);centerText(160,25,0.6f,ACCENT,"MAIN MENU");centerText(160,70,0.45f,WHITE,"D-Pad: select   A: open");centerText(160,105,0.39f,DIM,"Character choice unlocks at 200 lines");centerText(160,185,0.4f,DIM,"START: exit");}
static void drawGallery(){drawText(18,18,0.72f,ACCENT,"CUTSCENES");const char* items[]={"INTRO","100 LINES","200 LINES","GUARDIANS ENDING","BACK"};for(int i=0;i<5;i++){if(i==galleryIndex)C2D_DrawRectSolid(20,62+i*34,0.2f,360,28,C2D_Color32(85,40,115,230));drawText(30,65+i*34,0.5f,i==galleryIndex?WHITE:DIM,"%s",items[i]);}C2D_TargetClear(botTarget,BG);C2D_SceneBegin(botTarget);centerText(160,70,0.5f,WHITE,"A: play");centerText(160,115,0.42f,DIM,"B: main menu");}
static void drawSettings(){drawText(18,18,0.72f,ACCENT,"OPTIONS");const char* names[]={"TETRIS LAYOUT","PIECE FALL MODE","BACK"};for(int i=0;i<3;i++){if(i==settingsIndex)C2D_DrawRectSolid(15,70+i*48,0.2f,370,38,C2D_Color32(85,40,115,230));drawText(25,77+i*48,0.52f,i==settingsIndex?WHITE:DIM,"%s",names[i]);if(i==0)drawText(230,77+i*48,0.5f,ACCENT,"%s",dualScreen?"DUAL SCREEN":"COMPACT");if(i==1)drawText(230,77+i*48,0.5f,ACCENT,"%s",phobosFall?"PHOBOS":"CLASSIC");}C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,35,0.52f,WHITE,"A / LEFT / RIGHT: change");centerText(160,82,0.42f,DIM,"DUAL: 10 rows top + 10 bottom");centerText(160,135,0.4f,ACCENT,"In game: A + B toggles");centerText(160,190,0.4f,DIM,"B: back");}

static void drawPause(){C2D_SceneBegin(topTarget);C2D_DrawRectSolid(0,0,0.89f,400,240,C2D_Color32(0,0,0,200));centerText(200,35,0.75f,ACCENT,"PAUSED");const char* p[]={"CONTINUE","RESTART","MAIN MENU"};for(int i=0;i<3;i++){if(i==pauseIndex)C2D_DrawRectSolid(90,88+i*35,0.91f,220,29,C2D_Color32(100,50,130,255));centerText(200,92+i*35,0.5f,i==pauseIndex?WHITE:DIM,p[i]);}C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,15,0.45f,ACCENT,"PAUSE MENU");for(int i=0;i<3;i++){C2D_DrawRectSolid(30,55+i*52,0.3f,260,38,i==pauseIndex?C2D_Color32(110,55,145,255):C2D_Color32(55,28,75,255));centerText(160,64+i*52,0.47f,i==pauseIndex?WHITE:DIM,p[i]);}centerText(160,218,0.34f,DIM,"D-Pad + A or touch");}
static void drawGameOver(){C2D_SceneBegin(topTarget);C2D_DrawRectSolid(0,0,0.9f,400,240,C2D_Color32(0,0,0,210));centerText(200,42,0.85f,RED,"GAME OVER");const char* p[]={"RESTART","MAIN MENU"};for(int i=0;i<2;i++){if(i==gameOverIndex)C2D_DrawRectSolid(95,112+i*42,0.92f,210,32,C2D_Color32(100,50,130,255));centerText(200,118+i*42,0.52f,i==gameOverIndex?WHITE:DIM,p[i]);}C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,32,0.58f,RED,"GAME OVER");for(int i=0;i<2;i++){C2D_DrawRectSolid(35,90+i*55,0.3f,250,40,i==gameOverIndex?C2D_Color32(110,55,145,255):C2D_Color32(55,28,75,255));centerText(160,100+i*55,0.48f,i==gameOverIndex?WHITE:DIM,p[i]);}centerText(160,210,0.34f,DIM,"UP/DOWN + A or touch");}
static void drawGame(){
 int bg=lines<100?0:(guardiansRoute?2:1);bgGame[bg].drawCover(0,0,400,240);C2D_DrawRectSolid(0,0,0.15f,400,240,C2D_Color32(0,0,0,105));
 if(dualScreen){drawBoardSlice(80,0,24,0,10);drawText(5,8,0.42f,ACCENT,"HOLD");if(holdType>=0)drawMiniPiece(holdType,4,38,10);drawText(326,8,0.42f,ACCENT,"NEXT");drawMiniPiece(nextType,328,38,10);drawText(318,100,0.38f,WHITE,"%d",score);drawText(318,125,0.34f,DIM,"L %d",lines);if(!guardiansRoute)phobosGame.drawFit(315,150,82,88,0.4f);C2D_TargetClear(botTarget,BG);C2D_SceneBegin(botTarget);drawBoardSlice(40,0,24,10,10);C2D_DrawRectSolid(2,198,0.9f,36,38,C2D_Color32(90,45,120,235));centerText(20,207,0.29f,WHITE,"KB");}
 else{if(!guardiansRoute)phobosGame.drawFit(290,32,105,200,0.2f);drawBoardSlice(118,18,10,0,20);drawText(10,20,0.45f,ACCENT,"HOLD");if(holdType>=0)drawMiniPiece(holdType,15,52,10);drawText(238,20,0.45f,ACCENT,"NEXT");drawMiniPiece(nextType,245,52,10);drawText(8,128,0.38f,WHITE,"SCORE %d",score);drawText(8,150,0.38f,WHITE,"LINES %d",lines);C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,15,0.46f,ACCENT,"CONTROLS");drawText(16,52,0.38f,WHITE,"D-Pad move/drop   A/B rotate");drawText(16,78,0.38f,WHITE,"Y hard drop       X hold");drawText(16,104,0.38f,WHITE,"L/R shuffled next track");drawText(16,130,0.38f,WHITE,"START/SELECT pause");drawText(16,156,0.36f,DIM,"A+B: dual-screen layout");C2D_DrawRectSolid(178,194,0.3f,126,36,C2D_Color32(90,45,120,235));centerText(241,203,0.42f,WHITE,"KEYBOARD / CODES");}
 if(!codeMessage.empty()){C2D_SceneBegin(topTarget);centerText(200,218,0.33f,ACCENT,codeMessage.c_str());}
 if(paused)drawPause();else if(gameOver)drawGameOver();
}

static void drawCharacterLine(Art* set,float y){for(int i=0;i<7;i++)set[i].drawFit(10+i*56,y,48,145,0.3f);}
static void drawCutscene(){
 if(cutsceneStage==0){introCastle.drawCover(0,0,400,240);C2D_DrawRectSolid(0,0,0.1f,400,240,C2D_Color32(0,0,0,85));if(cutscenePage==0){introPhobos.drawFit(258,20,130,215,0.3f);drawText(15,18,0.56f,ACCENT,"PHOBOS CASTLE");drawText(15,192,0.38f,WHITE,"Заклинание уже началось.");}else if(cutscenePage==1){drawCharacterLine(introNormal,42);centerText(200,18,0.5f,WHITE,"СТРАЖНИЦЫ ПРИШЛИ СЛИШКОМ ПОЗДНО");}else if(cutscenePage==2){drawCharacterLine(introFinal,42);centerText(200,18,0.5f,ACCENT,"ФОБОС ПРЕВРАЩАЕТ ИХ В ФИГУРЫ");}else{introPhobos.drawFit(120,10,160,220,0.3f);centerText(200,195,0.42f,WHITE,"ИГРА ТОЛЬКО НАЧИНАЕТСЯ");}}
 else if(cutsceneStage==100){if(cutscenePage==0){l100Will.drawFit(15,22,165,205);l100Phobos.drawFit(215,12,170,215);centerText(200,8,0.48f,ACCENT,"100 LINES - RESISTANCE");centerText(200,208,0.35f,WHITE,"ФОБОС: Ты всё ещё сопротивляешься?");}else if(cutscenePage==1){drawCharacterLine(introNormal,38);centerText(200,18,0.5f,WHITE,"СТРАЖНИЦЫ ВСПОМИНАЮТ СЕБЯ");centerText(200,202,0.36f,ACCENT,"ЕЩЁ СТО ЛИНИЙ");}else{l100Phobos.drawFit(105,12,190,215);centerText(200,205,0.36f,WHITE,"ФОБОС: Теперь станет интереснее.");}}
 else{if(cutscenePage==0){drawCharacterLine(introFinal,38);centerText(200,16,0.49f,ACCENT,"200 LINES - SPELL BREAKS");centerText(200,202,0.36f,WHITE,"ЗАКЛИНАНИЕ РАЗРУШАЕТСЯ...");}else if(cutscenePage==1){endingHeart.drawFit(90,10,220,200);centerText(200,205,0.38f,WHITE,"СТРАЖНИЦЫ: МЫ СНОВА ВМЕСТЕ!");}else if(cutscenePage==2){l100Phobos.drawFit(105,8,190,220);centerText(200,202,0.34f,RED,"ФОБОС: НЕТ... ЭТО НЕВОЗМОЖНО!");}else{introCastle.drawCover(0,0,400,240);C2D_DrawRectSolid(0,0,0.2f,400,240,C2D_Color32(35,0,45,155));centerText(200,90,0.72f,ACCENT,"КТО ПОБЕДИТ?");centerText(200,145,0.4f,WHITE,"РЕШЕНИЕ ПЕРЕХОДИТ К ТЕБЕ");}}
 C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,72,0.52f,WHITE,"A / TOUCH: next frame");centerText(160,120,0.42f,DIM,"B / START: skip scene");centerText(160,178,0.34f,ACCENT,"This scene contains several frames");
}
static void drawWinner(){endingHeart.drawFit(130,5,140,105);centerText(200,112,0.68f,ACCENT,"WHO WINS?");const char* opts[]={"GUARDIANS","PHOBOS"};for(int i=0;i<2;i++){float x=24+i*190;if(i==winnerChoice)C2D_DrawRectSolid(x,158,0.2f,162,45,C2D_Color32(95,45,120,230));centerText(x+81,170,0.48f,i==winnerChoice?WHITE:DIM,opts[i]);}C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,25,0.48f,WHITE,"LEFT / RIGHT + A");C2D_DrawRectSolid(20,72,0.2f,280,70,C2D_Color32(80,35,105,255));centerText(160,91,0.58f,WHITE,"ENTER SECRET CODE");centerText(160,155,0.38f,ACCENT,"Touch the box or press X");centerText(160,190,0.31f,DIM,"VTD MATRIX JTX GPT SUNO PORN");}
static void drawEnding(){endingArt[endingPage%7].drawFit(80,4,240,205);centerText(200,210,0.48f,ACCENT,"GUARDIANS WIN");C2D_TargetClear(botTarget,BG);C2D_SceneBegin(botTarget);centerText(160,60,0.55f,WHITE,charNames[endingPage%7]);centerText(160,112,0.4f,ACCENT,"A / TOUCH: next ending frame");centerText(160,165,0.36f,DIM,"The cartoon ending returns to menu");}
static void drawRoom(){roomBg.drawCover(0,0,400,240);roomPoses[roomPose].drawFit(225,18,165,215,0.3f);roomFg.drawFit(0,0,400,240,0.6f);C2D_TargetClear(botTarget,C2D_Color32(15,6,20,255));C2D_SceneBegin(botTarget);centerText(160,20,0.58f,ACCENT,"PHOBOS ROOM");centerText(160,72,0.34f,WHITE,roomLines[roomLine%ROOM_LINE_COUNT]);centerText(160,125,0.38f,DIM,"A: next line     X: pose");centerText(160,157,0.36f,DIM,"Выхода в меню здесь нет.");centerText(160,185,0.34f,DIM,"Только закрытие программы.");}
static void drawVideo(){videoFrame.drawCover(0,0,400,240);C2D_TargetClear(botTarget,C2D_Color32(0,0,0,255));C2D_SceneBegin(botTarget);centerText(160,80,0.52f,videoKind==1?GREEN:WHITE,videoKind==1?"MATRIX":"PORN INTRO");centerText(160,125,0.38f,DIM,videoKind==1?"The planned exit follows the clip":"Returns to WHO WINS?");centerText(160,175,0.35f,WHITE,"A / B: skip");}
static void drawVtd(){vtdObs.drawFit(0,0,400,240);C2D_TargetClear(botTarget,C2D_Color32(10,5,20,255));C2D_SceneBegin(botTarget);centerText(160,70,0.58f,ACCENT,"VTD / VALENTIN");centerText(160,115,0.43f,WHITE,"The game closes when the track ends");centerText(160,145,0.38f,DIM,"L + R: close now");}
static void drawPornGallery(){C2D_TargetClear(topTarget,C2D_Color32(32,8,24,255));pornArts[pornImage%2].drawFit(8,8,205,224);phobosMenu.drawFit(238,18,152,205);drawText(218,25,0.50f,ACCENT,"PORN");drawText(214,176,0.31f,WHITE,"Ну зачем ты ввёл");drawText(214,197,0.31f,WHITE,"код порно?");C2D_TargetClear(botTarget,C2D_Color32(16,5,14,255));C2D_SceneBegin(botTarget);centerText(160,75,0.46f,WHITE,"Случайная картинка + реакция Фобоса");centerText(160,125,0.44f,DIM,"A / B: return");}
static void drawJetix(){jetixLogo.drawFit(95,25,210,165);centerText(200,198,0.6f,WHITE,"THANK YOU, JETIX");C2D_TargetClear(botTarget,BG);C2D_SceneBegin(botTarget);centerText(160,80,0.55f,ACCENT,"JTX is its own code");centerText(160,135,0.4f,DIM,"A / B: back");}
static void drawCard(){Art& logo=secretTimer==0?chatgptLogo:sunoLogo;C2D_DrawRectSolid(35,20,0.1f,330,190,secretTimer==0?C2D_Color32(20,30,32,255):C2D_Color32(55,28,105,255));C2D_DrawRectSolid(42,27,0.2f,316,176,C2D_Color32(245,245,245,255));logo.drawFit(135,42,130,105,0.3f);centerText(200,166,0.65f,C2D_Color32(20,20,25,255),secretTimer==0?"CHATGPT":"SUNO");C2D_TargetClear(botTarget,BG);C2D_SceneBegin(botTarget);centerText(160,68,0.48f,WHITE,"Offline 3DS card");centerText(160,115,0.38f,DIM,"No browser is opened");centerText(160,165,0.4f,ACCENT,"A / B: return");}

static void render(){C3D_FrameBegin(C3D_FRAME_SYNCDRAW);C2D_TargetClear(topTarget,BG);C2D_SceneBegin(topTarget);switch(mode){case MENU:drawMenu();break;case GALLERY:drawGallery();break;case SETTINGS:drawSettings();break;case GAME:drawGame();break;case CUTSCENE:drawCutscene();break;case WINNER:drawWinner();break;case ENDING:drawEnding();break;case PHOBOS_ROOM:drawRoom();break;case VIDEO_MODE:drawVideo();break;case VTD_MODE:drawVtd();break;case PORN_GALLERY:drawPornGallery();break;case JETIX_MODE:drawJetix();break;case CARD_MODE:drawCard();break;}C3D_FrameEnd(0);}

static void finishCutscene(){music.autoAdvance=true;if(cutsceneStage==100&&cutsceneReturn==GAME){mode=GAME;music.start(2);}else if(cutsceneStage==200&&cutsceneReturn==WINNER){mode=WINNER;winnerChoice=0;music.start(0);audio.play("romfs:/audio/music_winner_choice.mp3",true);}else{mode=cutsceneReturn;if(mode==MENU)music.start(0);}}
static int cutscenePages(){return cutsceneStage==100?3:4;}
static void activatePause(){if(pauseIndex==0){paused=false;audio.setPause(false);}else if(pauseIndex==1)newGame();else{paused=false;mode=MENU;music.start(0);}}
static void activateGameOver(){if(gameOverIndex==0)newGame();else{gameOver=false;mode=MENU;music.start(0);}}

static void handleInput(u32 kd,u32 kh,touchPosition tp){
 if(mode==MENU){if(kd&KEY_UP)menuIndex=(menuIndex+3)%4;if(kd&KEY_DOWN)menuIndex=(menuIndex+1)%4;if(kd&KEY_A){if(menuIndex==0)newGame();else if(menuIndex==1){mode=GALLERY;galleryIndex=0;}else if(menuIndex==2)mode=SETTINGS;else running=false;}if(kd&KEY_START)running=false;return;}
 if(mode==GALLERY){if(kd&KEY_UP)galleryIndex=(galleryIndex+4)%5;if(kd&KEY_DOWN)galleryIndex=(galleryIndex+1)%5;if(kd&KEY_A){if(galleryIndex==0)startCutscene(0,GALLERY);else if(galleryIndex==1)startCutscene(100,GALLERY);else if(galleryIndex==2)startCutscene(200,GALLERY);else if(galleryIndex==3){mode=ENDING;returnMode=GALLERY;endingPage=0;}else mode=MENU;}if(kd&KEY_B)mode=MENU;return;}
 if(mode==SETTINGS){if(kd&KEY_UP)settingsIndex=(settingsIndex+2)%3;if(kd&KEY_DOWN)settingsIndex=(settingsIndex+1)%3;if(kd&(KEY_LEFT|KEY_RIGHT|KEY_A)){if(settingsIndex==0){dualScreen=!dualScreen;saveSettings();}else if(settingsIndex==1){phobosFall=!phobosFall;saveSettings();}else mode=MENU;}if(kd&KEY_B)mode=MENU;return;}
 if(mode==GAME){
  if(gameOver){if(kd&KEY_UP||kd&KEY_DOWN)gameOverIndex^=1;if(kd&KEY_A)activateGameOver();if(kd&KEY_B){gameOverIndex=1;activateGameOver();}if(kd&KEY_TOUCH){for(int i=0;i<2;i++){int y=90+i*55;if(tp.px>=35&&tp.px<=285&&tp.py>=y&&tp.py<y+40){gameOverIndex=i;activateGameOver();break;}}}return;}
  if(paused){if(kd&KEY_UP)pauseIndex=(pauseIndex+2)%3;if(kd&KEY_DOWN)pauseIndex=(pauseIndex+1)%3;if(kd&(KEY_A|KEY_Y|KEY_RIGHT))activatePause();if(kd&(KEY_B|KEY_START|KEY_SELECT)){paused=false;audio.setPause(false);}if(kd&KEY_TOUCH){for(int i=0;i<3;i++){int y=55+i*52;if(tp.px>=30&&tp.px<=290&&tp.py>=y&&tp.py<y+38){pauseIndex=i;activatePause();break;}}}return;}
  if(kd&(KEY_START|KEY_SELECT)){paused=true;pauseIndex=0;audio.setPause(true);return;}
  bool ab=((kh&(KEY_A|KEY_B))==(KEY_A|KEY_B))&&(kd&(KEY_A|KEY_B));if(ab){dualScreen=!dualScreen;saveSettings();return;}
  if(kd&KEY_TOUCH){bool hit=dualScreen?(tp.px<=40&&tp.py>=190):(tp.px>=178&&tp.px<=304&&tp.py>=188);if(hit){openCodeKeyboard();return;}}
  if(kd&KEY_LEFT&&fits(curType,curRot,curX-1,curY))curX--;if(kd&KEY_RIGHT&&fits(curType,curRot,curX+1,curY))curX++;if(kh&KEY_DOWN&&frameCounter%3==0){if(fits(curType,curRot,curX,curY+1))curY++;else lockPiece();}if(kd&(KEY_A|KEY_B|KEY_UP)){int nr=(curRot+1)&3;if(fits(curType,nr,curX,curY))curRot=nr;}if(kd&KEY_Y)hardDrop();if(kd&KEY_X)hold();if(kd&(KEY_L|KEY_R))music.playNext();return;
 }
 if(mode==CUTSCENE){if(kd&(KEY_A|KEY_TOUCH)){cutscenePage++;if(cutscenePage>=cutscenePages())finishCutscene();}if(kd&(KEY_B|KEY_START)){cutscenePage=cutscenePages();finishCutscene();}return;}
 if(mode==WINNER){if(kd&KEY_LEFT)winnerChoice=0;if(kd&KEY_RIGHT)winnerChoice=1;if(kd&KEY_A)chooseWinner();if(kd&KEY_X)openCodeKeyboard();if((kd&KEY_TOUCH)&&tp.px>=20&&tp.px<=300&&tp.py>=72&&tp.py<=142)openCodeKeyboard();return;}
 if(mode==ENDING){if(kd&(KEY_A|KEY_TOUCH)){endingPage++;if(endingPage>=7){mode=(returnMode==GALLERY)?GALLERY:MENU;music.start(0);}}if(kd&KEY_B){mode=MENU;music.start(0);}return;}
 if(mode==PHOBOS_ROOM){if(kd&KEY_X)roomPose=(roomPose+1)%6;if(kd&KEY_A){roomPose=(roomPose+1)%6;roomLine=(roomLine+1+rand()%3)%ROOM_LINE_COUNT;}return;}
 if(mode==VIDEO_MODE){if(kd&(KEY_A|KEY_B|KEY_START)){if(videoKind==1)running=false;else{videoFrame.free();mode=returnMode;music.autoAdvance=true;music.playNext();}}return;}
 if(mode==VTD_MODE){if(((kh&KEY_L)&&(kh&KEY_R))||(kd&KEY_START))running=false;return;}
 if(mode==PORN_GALLERY||mode==JETIX_MODE||mode==CARD_MODE){if(kd&(KEY_A|KEY_B|KEY_START)){mode=returnMode;music.autoAdvance=true;music.playNext();}return;}
}

static void updateVideo(){int idx=(videoTick*videoFps)/60;if(idx>=videoCount||audio.finished){if(videoKind==1)running=false;else{videoFrame.free();mode=returnMode;music.autoAdvance=true;music.playNext();}return;}if(idx!=videoLoaded){videoLoaded=idx;videoFrame.free();char p[96];snprintf(p,sizeof(p),videoKind==1?"romfs:/video/matrix/matrix_%03d.t3x":"romfs:/video/porn/porn_%03d.t3x",idx);videoFrame.load(p);}videoTick++;}
static void update(){frameCounter++;music.update();if(mode==VIDEO_MODE)updateVideo();if(mode==VTD_MODE&&audio.finished)running=false;if(mode==JETIX_MODE&&secretTimer>0){secretTimer--;if(!secretTimer){mode=returnMode;music.autoAdvance=true;music.playNext();}}if(mode==GAME&&!paused&&!gameOver&&frameCounter%(std::max(8,35-level*2))==0){if(fits(curType,curRot,curX,curY+1))curY++;else lockPiece();}}

static void loadArt(){
 bgMenu.load("romfs:/gfx/bg_menu.t3x");bgGame[0].load("romfs:/gfx/bg_phase0.t3x");bgGame[1].load("romfs:/gfx/bg_phase1.t3x");bgGame[2].load("romfs:/gfx/bg_phase2.t3x");phobosMenu.load("romfs:/gfx/phobos_menu_body.t3x");phobosGame.load("romfs:/gfx/phobos_gameplay.t3x");phaseCells.load("romfs:/gfx/phase1_cells.t3x");horrorCells.load("romfs:/gfx/horror_cells.t3x");vtdObs.load("romfs:/gfx/vtd_observer.t3x");roomBg.load("romfs:/gfx/phobos_room_bg.t3x");roomFg.load("romfs:/gfx/phobos_room_foreground.t3x");
 for(int i=0;i<6;i++){char p[80];snprintf(p,sizeof(p),"romfs:/gfx/phobos_room_pose%d.t3x",i);roomPoses[i].load(p);}introCastle.load("romfs:/gfx/intro_castle.t3x");introPhobos.load("romfs:/gfx/intro_phobos.t3x");
 for(int i=0;i<7;i++){char p[96];snprintf(p,sizeof(p),"romfs:/gfx/intro_%s.t3x",charFiles[i]);introNormal[i].load(p);snprintf(p,sizeof(p),"romfs:/gfx/intro_%s_final.t3x",charFiles[i]);introFinal[i].load(p);snprintf(p,sizeof(p),"romfs:/gfx/ending_%s.t3x",charFiles[i]);endingArt[i].load(p);}
 l100Will.load("romfs:/gfx/l100_will.t3x");l100Phobos.load("romfs:/gfx/l100_phobos.t3x");endingHeart.load("romfs:/gfx/ending_heart.t3x");pornArts[0].load("romfs:/gfx/secret_porn0.t3x");pornArts[1].load("romfs:/gfx/secret_porn1.t3x");jetixLogo.load("romfs:/gfx/jetix_logo.t3x");chatgptLogo.load("romfs:/gfx/logo_chatgpt.t3x");sunoLogo.load("romfs:/gfx/logo_suno.t3x");
}
static void freeArt(){bgMenu.free();for(auto& a:bgGame)a.free();phobosMenu.free();phobosGame.free();phaseCells.free();horrorCells.free();vtdObs.free();roomBg.free();roomFg.free();for(auto& a:roomPoses)a.free();introCastle.free();introPhobos.free();for(auto& a:introNormal)a.free();for(auto& a:introFinal)a.free();for(auto& a:endingArt)a.free();l100Will.free();l100Phobos.free();endingHeart.free();for(auto& a:pornArts)a.free();jetixLogo.free();chatgptLogo.free();sunoLogo.free();videoFrame.free();}

int main(){srand((unsigned)time(nullptr));gfxInitDefault();romfsInit();cfguInit();C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);C2D_Init(C2D_DEFAULT_MAX_OBJECTS);C2D_Prepare();topTarget=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);botTarget=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);textBuf=C2D_TextBufNew(4096);sysFont=C2D_FontLoadSystem(CFG_REGION_EUR);loadSettings();loadArt();audio.init();startCutscene(0,MENU);while(aptMainLoop()&&running){hidScanInput();u32 kd=hidKeysDown(),kh=hidKeysHeld();touchPosition tp;hidTouchRead(&tp);handleInput(kd,kh,tp);update();render();}audio.fini();freeArt();C2D_FontFree(sysFont);C2D_TextBufDelete(textBuf);C2D_Fini();C3D_Fini();cfguExit();romfsExit();gfxExit();return 0;}
