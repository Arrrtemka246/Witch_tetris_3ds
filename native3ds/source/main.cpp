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
        C2D_ImageTint tint; C2D_PlainImageTint(&tint,C2D_Color32(255,255,255,(u8)(255*alpha)),1.0f);
        C2D_DrawImageAt(im,dx,dy,z,&tint,s,s);
    }
    void drawCover(float x,float y,float w,float h,float z=0.1f) const {
        if(!sheet) return; C2D_Image im=C2D_SpriteSheetGetImage(sheet,0);
        float iw=(float)im.subtex->width, ih=(float)im.subtex->height;
        float s=std::max(w/iw,h/ih); float dx=x+(w-iw*s)/2, dy=y+(h-ih*s)/2;
        C2D_DrawImageAt(im,dx,dy,z,nullptr,s,s);
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
    std::vector<std::string> pool, bag; std::string fixedFirst,last; bool firstPending=true; int phase=-1;
    void configure(int p){
        if(phase==p) return; phase=p; pool.clear(); bag.clear(); firstPending=true;
        if(p==0){ fixedFirst="romfs:/audio/menu_1.mp3"; pool={"romfs:/audio/menu_1.mp3","romfs:/audio/menu_2.mp3"}; }
        else if(p==1){ fixedFirst="romfs:/audio/phase0.mp3"; pool={"romfs:/audio/phase0.mp3","romfs:/audio/music_arcade_1.mp3","romfs:/audio/music_arcade_3.mp3","romfs:/audio/music_arcade_6.mp3"}; }
        else if(p==2){ fixedFirst="romfs:/audio/phase1_1.mp3"; pool={"romfs:/audio/phase1_1.mp3","romfs:/audio/phase1_2.mp3","romfs:/audio/phase1_3.mp3","romfs:/audio/music_phase1_hollow.mp3"}; }
        else if(p==3){ fixedFirst="romfs:/audio/phase2_guardians.mp3"; pool={"romfs:/audio/phase2_guardians.mp3","romfs:/audio/music_arcade_2.mp3","romfs:/audio/music_arcade_4.mp3","romfs:/audio/music_arcade_5.mp3"}; }
        else { fixedFirst="romfs:/audio/phase2_phobos.mp3"; pool={"romfs:/audio/phase2_phobos.mp3","romfs:/audio/music_empty_hollow_1.mp3","romfs:/audio/music_empty_hollow_2.mp3","romfs:/audio/music_crucified.mp3","romfs:/audio/music_crusified2.mp3"}; }
    }
    std::string nextPath(){
        if(firstPending){ firstPending=false; last=fixedFirst; return last; }
        if(bag.empty()){
            bag=pool; std::random_shuffle(bag.begin(),bag.end());
            if(bag.size()>1 && bag.back()==last) std::swap(bag.front(),bag.back());
        }
        std::string p=bag.back(); bag.pop_back();
        if(p==last && !bag.empty()){ p=bag.back(); bag.pop_back(); }
        last=p; return p;
    }
    void playNext(){ std::string p=nextPath(); audio.play(p.c_str()); }
    void start(int p){ configure(p); playNext(); }
    void update(){ audio.update(); if(audio.finished){ audio.finished=false; playNext(); } }
} music;

enum Mode { MENU, CHARSEL, SETTINGS, GAME, CUTSCENE, WINNER, PHOBOS_ROOM, MATRIX_MODE, VTD_MODE, PORN_MODE };
static Mode mode=MENU, returnMode=CHARSEL;
static bool running=true, paused=false, gameOver=false, dualScreen=false, phobosFall=true;
static int menuIndex=0, settingsIndex=0, pauseIndex=0, charIndex=0, cutsceneStage=0, roomPose=0;
static int score=0, lines=0, level=1, frameCounter=0, secretTimer=0;
static int board[BH][BW]{};
static int curType=0,curRot=0,curX=3,curY=-1,nextType=1,holdType=-1; static bool holdUsed=false;
static int bag[7],bagPos=7;
static const char* charNames[7]={"CORNELIA","BLUNK","CALEB","IRMA","WILL","TARANEE","HAY LIN"};
static const char* charArts[7]={"romfs:/gfx/intro_cornelia.t3x","romfs:/gfx/intro_blunk.t3x","romfs:/gfx/intro_caleb.t3x","romfs:/gfx/intro_irma.t3x","romfs:/gfx/intro_will.t3x","romfs:/gfx/intro_taranee.t3x","romfs:/gfx/intro_haylin.t3x"};
static bool phobosRoute=false, guardiansRoute=false;

static Art bgMenu,phobosMenu,phobosGame,vtdObs,roomBg,roomFg,roomPoses[6],charArt[7],introCastle,introPhobos,l100Will,l100Phobos,endingHeart;

static void saveSettings(){ mkdir("sdmc:/3ds",0777); mkdir("sdmc:/3ds/WitchTetris",0777); FILE* f=fopen("sdmc:/3ds/WitchTetris/settings.cfg","w"); if(f){ fprintf(f,"dual=%d\nphobosfall=%d\n",dualScreen?1:0,phobosFall?1:0); fclose(f);} }
static void loadSettings(){ FILE* f=fopen("sdmc:/3ds/WitchTetris/settings.cfg","r"); if(!f)return; char k[64];int v; while(fscanf(f,"%63[^=]=%d\n",k,&v)==2){ if(!strcmp(k,"dual"))dualScreen=v; if(!strcmp(k,"phobosfall"))phobosFall=v; } fclose(f); }

static const int baseShape[7][4][2]={
 {{0,1},{1,1},{2,1},{3,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{0,1},{1,1},{2,1}},
 {{1,0},{2,0},{0,1},{1,1}}, {{0,0},{1,0},{1,1},{2,1}}, {{0,0},{0,1},{1,1},{2,1}}, {{2,0},{0,1},{1,1},{2,1}}
};
static void blockPos(int t,int r,int i,int& x,int& y){ x=baseShape[t][i][0]; y=baseShape[t][i][1]; if(t==1)return; for(int k=0;k<r;k++){ int nx=3-y,ny=x;x=nx;y=ny; } }
static bool fits(int t,int r,int px,int py){ for(int i=0;i<4;i++){int x,y;blockPos(t,r,i,x,y);x+=px;y+=py;if(x<0||x>=BW||y>=BH)return false;if(y>=0&&board[y][x])return false;}return true; }
static int nextBag(){ if(bagPos>=7){ for(int i=0;i<7;i++)bag[i]=i; for(int i=6;i>0;i--){int j=rand()%(i+1);std::swap(bag[i],bag[j]);}bagPos=0;} return bag[bagPos++]; }
static int nextPiece(){ if(!phobosFall) return nextBag(); if(rand()%100<45) return rand()%7; return nextBag(); }
static void spawn(){ curType=nextType; nextType=nextPiece();curRot=0;curX=3;curY=-1;holdUsed=false;if(!fits(curType,curRot,curX,curY)){gameOver=true;if(phobosRoute){mode=PHOBOS_ROOM;audio.play("romfs:/audio/phobos_room.mp3",true);}} }
static void newGame(){ memset(board,0,sizeof(board));score=lines=0;level=1;gameOver=false;paused=false;holdType=-1;holdUsed=false;bagPos=7;phobosRoute=guardiansRoute=false;nextType=nextPiece();spawn();mode=GAME;music.start(1); }
static void clearLines(){ int cleared=0; for(int y=BH-1;y>=0;y--){ bool full=true;for(int x=0;x<BW;x++)if(!board[y][x]){full=false;break;} if(full){cleared++;for(int yy=y;yy>0;yy--)memcpy(board[yy],board[yy-1],sizeof(board[0]));memset(board[0],0,sizeof(board[0]));y++;}}
 if(cleared){lines+=cleared;score+=100*cleared*cleared;level=1+lines/10;if(lines>=100 && lines-cleared<100){mode=CUTSCENE;cutsceneStage=100;audio.play("romfs:/audio/music_cutscene_lines100.mp3");} if(lines>=200&&lines-cleared<200){mode=WINNER;audio.play("romfs:/audio/music_winner_choice.mp3",true);} if(phobosRoute&&lines>=300){mode=PHOBOS_ROOM;audio.play("romfs:/audio/phobos_room.mp3",true);} }
}
static void lockPiece(){ for(int i=0;i<4;i++){int x,y;blockPos(curType,curRot,i,x,y);x+=curX;y+=curY;if(y>=0&&y<BH&&x>=0&&x<BW)board[y][x]=curType+1;}clearLines(); if(mode==GAME)spawn(); }
static void hardDrop(){ while(fits(curType,curRot,curX,curY+1)){curY++;score+=2;}lockPiece(); }
static void hold(){ if(holdUsed)return; if(holdType<0){holdType=curType;spawn();}else{std::swap(holdType,curType);curRot=0;curX=3;curY=-1;}holdUsed=true; }

static u32 pieceColor(int v){ static u32 c[8]={0,C2D_Color32(80,220,255,255),C2D_Color32(255,220,70,255),C2D_Color32(190,90,255,255),C2D_Color32(80,240,130,255),C2D_Color32(255,80,100,255),C2D_Color32(80,110,255,255),C2D_Color32(255,145,60,255)}; return c[v&7]; }
static void drawBoardSlice(float x0,float y0,float cell,int yStart,int count){
 C2D_DrawRectSolid(x0-2,y0-2,0.2f,BW*cell+4,count*cell+4,C2D_Color32(90,55,120,255)); C2D_DrawRectSolid(x0,y0,0.3f,BW*cell,count*cell,C2D_Color32(5,5,12,255));
 for(int yy=0;yy<count;yy++){int y=yStart+yy;for(int x=0;x<BW;x++)if(board[y][x])C2D_DrawRectSolid(x0+x*cell+1,y0+yy*cell+1,0.5f,cell-2,cell-2,pieceColor(board[y][x]));}
 for(int i=0;i<4;i++){int x,y;blockPos(curType,curRot,i,x,y);x+=curX;y+=curY;if(y>=yStart&&y<yStart+count)C2D_DrawRectSolid(x0+x*cell+1,y0+(y-yStart)*cell+1,0.6f,cell-2,cell-2,pieceColor(curType+1));}
}
static void drawMiniPiece(int type,float x,float y,float cell){for(int i=0;i<4;i++){int bx,by;blockPos(type,0,i,bx,by);C2D_DrawRectSolid(x+bx*cell,y+by*cell,0.5f,cell-1,cell-1,pieceColor(type+1));}}

static void openCodeKeyboard(){
 char buf[64]={0}; SwkbdState sw; swkbdInit(&sw,SWKBD_TYPE_NORMAL,1,32); swkbdSetHintText(&sw,"MATRIX / VTD / PORN / МАТРИЦА / ВТД / ПОРН"); swkbdSetButton(&sw,SWKBD_BUTTON_RIGHT,"OK",true); swkbdSetFeatures(&sw,SWKBD_DEFAULT_QWERTY|SWKBD_ALLOW_HOME);
 SwkbdButton b=swkbdInputText(&sw,buf,sizeof(buf)); if(b==SWKBD_BUTTON_NONE)return;
 std::string s=buf; for(char& c:s) if((unsigned char)c<128)c=(char)std::tolower((unsigned char)c);
 if(s=="matrix"||s=="матрица"){returnMode=mode;mode=MATRIX_MODE;secretTimer=60*12;audio.play("romfs:/audio/voice_matrix.mp3");}
 else if(s=="vtd"||s=="valentin"||s=="втд"||s=="валентин"){returnMode=mode;mode=VTD_MODE;secretTimer=0;audio.play((rand()%2)?"romfs:/audio/vtd_1.mp3":"romfs:/audio/vtd_2.mp3");}
 else if(s=="porn"||s=="порн"){returnMode=mode;mode=PORN_MODE;secretTimer=60*8;audio.play("romfs:/audio/voice_porn.mp3");}
}

static void drawMenu(){
 bgMenu.drawCover(0,0,TOP_W,H); C2D_DrawRectSolid(0,0,0.2f,TOP_W,H,C2D_Color32(0,0,0,90)); phobosMenu.drawFit(250,25,145,210,0.3f);
 drawText(18,20,0.72f,ACCENT,"W.I.T.C.H. TETRIS 3DS"); drawText(20,48,0.45f,WHITE,"RC1 - editable native rebuild");
 const char* items[]={"NEW GAME","CUTSCENES","SETTINGS","EXIT"}; for(int i=0;i<4;i++){u32 col=i==menuIndex?WHITE:DIM;if(i==menuIndex)C2D_DrawRectSolid(18,82+i*34,0.4f,205,28,C2D_Color32(95,45,120,220));drawText(28,85+i*34,0.55f,col,"> %s",items[i]);}
 C2D_TargetClear(botTarget,BG); C2D_SceneBegin(botTarget); centerText(160,25,0.6f,ACCENT,"MAIN MENU"); centerText(160,70,0.45f,WHITE,"D-Pad: select   A: open"); centerText(160,100,0.42f,DIM,"Phobos Room is gameplay-only"); centerText(160,185,0.4f,DIM,"START: exit");
}
static void drawCharSelect(){
 C2D_TargetClear(topTarget,BG); charArt[charIndex].drawFit(210,10,180,225); drawText(15,18,0.68f,ACCENT,"CHOOSE CHARACTER");drawText(18,65,0.72f,WHITE,"%s",charNames[charIndex]);drawText(18,115,0.44f,DIM,"LEFT / RIGHT: choose");drawText(18,142,0.44f,DIM,"A: start game");drawText(18,169,0.44f,ACCENT,"X: ENTER SECRET CODE");
 C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);C2D_DrawRectSolid(20,40,0.2f,280,70,C2D_Color32(80,35,105,255));centerText(160,58,0.58f,WHITE,"ENTER CODE");centerText(160,132,0.42f,DIM,"Touch here or press X");centerText(160,190,0.4f,DIM,"B: back");
}
static void drawSettings(){
 C2D_TargetClear(topTarget,BG);drawText(18,18,0.72f,ACCENT,"OPTIONS");const char* names[]={"TETRIS LAYOUT","PIECE FALL MODE","BACK"}; for(int i=0;i<3;i++){if(i==settingsIndex)C2D_DrawRectSolid(15,70+i*48,0.2f,370,38,C2D_Color32(85,40,115,230));drawText(25,77+i*48,0.52f,i==settingsIndex?WHITE:DIM,"%s",names[i]); if(i==0)drawText(230,77+i*48,0.5f,ACCENT,"%s",dualScreen?"DUAL SCREEN":"COMPACT");if(i==1)drawText(230,77+i*48,0.5f,ACCENT,"%s",phobosFall?"PHOBOS":"CLASSIC");}
 C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,35,0.52f,WHITE,"A / LEFT / RIGHT: change");centerText(160,78,0.42f,DIM,"DUAL: 10 rows top + 10 rows bottom");centerText(160,105,0.42f,DIM,"24 px cells, centered on both screens");centerText(160,180,0.4f,DIM,"B: back");
}
static void drawGame(){
 C2D_TargetClear(topTarget,BG);
 if(dualScreen){
   drawBoardSlice(80,0,24,0,10); drawText(5,8,0.42f,ACCENT,"HOLD");if(holdType>=0)drawMiniPiece(holdType,4,38,10);drawText(326,8,0.42f,ACCENT,"NEXT");drawMiniPiece(nextType,328,38,10);drawText(318,100,0.38f,WHITE,"%d",score);drawText(318,125,0.34f,DIM,"L %d",lines);phobosGame.drawFit(315,150,82,88,0.4f);
   C2D_TargetClear(botTarget,BG);C2D_SceneBegin(botTarget);drawBoardSlice(40,0,24,10,10);
 }else{
   phobosGame.drawFit(290,32,105,200,0.2f);drawBoardSlice(118,18,10,0,20);drawText(10,20,0.45f,ACCENT,"HOLD");if(holdType>=0)drawMiniPiece(holdType,15,52,10);drawText(238,20,0.45f,ACCENT,"NEXT");drawMiniPiece(nextType,245,52,10);drawText(8,128,0.38f,WHITE,"SCORE %d",score);drawText(8,150,0.38f,WHITE,"LINES %d",lines);
   C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,18,0.48f,ACCENT,"CONTROLS");drawText(18,58,0.4f,WHITE,"D-Pad  move/drop");drawText(18,84,0.4f,WHITE,"A/B     rotate");drawText(18,110,0.4f,WHITE,"Y       hard drop");drawText(18,136,0.4f,WHITE,"X       hold");drawText(18,162,0.4f,WHITE,"L/R     shuffled next track");drawText(18,188,0.4f,WHITE,"START/SELECT pause");
 }
 if(paused){ C2D_SceneBegin(topTarget);C2D_DrawRectSolid(0,0,0.89f,400,240,C2D_Color32(0,0,0,190));centerText(200,35,0.75f,ACCENT,"PAUSED");const char* p[]={"CONTINUE","RESTART","MAIN MENU"};for(int i=0;i<3;i++){if(i==pauseIndex)C2D_DrawRectSolid(90,88+i*35,0.91f,220,29,C2D_Color32(100,50,130,255));centerText(200,92+i*35,0.5f,i==pauseIndex?WHITE:DIM,p[i]);} }
 if(gameOver){ C2D_SceneBegin(topTarget);C2D_DrawRectSolid(0,0,0.9f,400,240,C2D_Color32(0,0,0,195));centerText(200,75,0.85f,RED,"GAME OVER");centerText(200,130,0.48f,WHITE,"A: restart   B: menu"); }
}
static void drawCutscene(){
 C2D_TargetClear(topTarget,BG); if(cutsceneStage==100){l100Phobos.drawFit(210,15,180,210);l100Will.drawFit(10,25,160,200);drawText(16,10,0.55f,ACCENT,"100 LINES - RESISTANCE");drawText(22,195,0.42f,WHITE,"PHOBOS: You are still resisting?");}
 else {introCastle.drawCover(0,0,400,240);introPhobos.drawFit(260,35,125,195,0.3f);drawText(15,15,0.55f,ACCENT,"PHOBOS CASTLE");drawText(15,195,0.42f,WHITE,"The spell has begun.");}
 C2D_TargetClear(botTarget,C2D_Color32(14,8,25,255));C2D_SceneBegin(botTarget);centerText(160,75,0.52f,WHITE,"A / TOUCH: next");centerText(160,120,0.42f,DIM,"B / START: skip");
}
static void drawWinner(){C2D_TargetClear(topTarget,BG);endingHeart.drawFit(130,10,140,110);centerText(200,125,0.68f,ACCENT,"WHO WINS?");centerText(200,170,0.52f,WHITE,guardiansRoute?"< GUARDIANS >":phobosRoute?"< PHOBOS >":"LEFT: GUARDIANS   RIGHT: PHOBOS");C2D_TargetClear(botTarget,BG);C2D_SceneBegin(botTarget);centerText(160,80,0.48f,WHITE,"LEFT / RIGHT choose");centerText(160,125,0.48f,WHITE,"A confirm");}
static void drawRoom(){C2D_TargetClear(topTarget,BG);roomBg.drawCover(0,0,400,240);roomPoses[roomPose].drawFit(225,18,165,215,0.3f);roomFg.drawFit(0,0,400,240,0.6f);C2D_TargetClear(botTarget,C2D_Color32(15,6,20,255));C2D_SceneBegin(botTarget);centerText(160,28,0.6f,ACCENT,"PHOBOS ROOM");centerText(160,80,0.43f,WHITE,"A: next line     X: pose");centerText(160,125,0.42f,DIM,"There is no menu exit from this room.");centerText(160,155,0.42f,DIM,"Close the software to leave.");}
static void drawMatrix(){C2D_TargetClear(topTarget,C2D_Color32(0,0,0,255));for(int i=0;i<80;i++){float x=(rand()%400),y=(rand()%240);C2D_DrawRectSolid(x,y,0.2f,2,4+(rand()%16),C2D_Color32(0,100+rand()%155,20,180));}centerText(200,95,0.8f,GREEN,"MATRIX");C2D_TargetClear(botTarget,C2D_Color32(0,0,0,255));C2D_SceneBegin(botTarget);centerText(160,95,0.45f,GREEN,"A / B: return safely");}
static void drawVTD(){C2D_TargetClear(topTarget,BG);vtdObs.drawFit(0,0,400,240);C2D_TargetClear(botTarget,C2D_Color32(10,5,20,255));C2D_SceneBegin(botTarget);centerText(160,70,0.58f,ACCENT,"VTD / VALENTIN");centerText(160,115,0.43f,WHITE,"L + R: exit VTD -> classic");}
static void drawPorn(){C2D_TargetClear(topTarget,C2D_Color32(32,8,24,255));phobosMenu.drawFit(225,10,165,220);drawText(16,35,0.72f,ACCENT,"PORN");drawText(16,86,0.5f,WHITE,"Ну зачем ты ввёл код порно?");drawText(16,126,0.38f,DIM,"Gallery images were missing from the");drawText(16,149,0.38f,DIM,"original 3DS RomFS; voice is restored.");C2D_TargetClear(botTarget,C2D_Color32(16,5,14,255));C2D_SceneBegin(botTarget);centerText(160,90,0.48f,WHITE,"A / B: return");}

static void render(){
 C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C2D_TargetClear(topTarget,BG); C2D_SceneBegin(topTarget);
 switch(mode){case MENU:drawMenu();break;case CHARSEL:drawCharSelect();break;case SETTINGS:drawSettings();break;case GAME:drawGame();break;case CUTSCENE:drawCutscene();break;case WINNER:drawWinner();break;case PHOBOS_ROOM:drawRoom();break;case MATRIX_MODE:drawMatrix();break;case VTD_MODE:drawVTD();break;case PORN_MODE:drawPorn();break;}
 C3D_FrameEnd(0);
}

static void handleInput(u32 kd,u32 kh,touchPosition tp){
 if(mode==MENU){if(kd&KEY_UP)menuIndex=(menuIndex+3)%4;if(kd&KEY_DOWN)menuIndex=(menuIndex+1)%4;if(kd&KEY_A){if(menuIndex==0)mode=CHARSEL;else if(menuIndex==1){mode=CUTSCENE;cutsceneStage=0;}else if(menuIndex==2)mode=SETTINGS;else running=false;}if(kd&KEY_START)running=false;return;}
 if(mode==CHARSEL){if(kd&KEY_LEFT)charIndex=(charIndex+6)%7;if(kd&KEY_RIGHT)charIndex=(charIndex+1)%7;if(kd&KEY_A)newGame();if(kd&KEY_B)mode=MENU;if(kd&KEY_X)openCodeKeyboard();if((kd&KEY_TOUCH)&&tp.px>=20&&tp.px<=300&&tp.py>=40&&tp.py<=110)openCodeKeyboard();return;}
 if(mode==SETTINGS){if(kd&KEY_UP)settingsIndex=(settingsIndex+2)%3;if(kd&KEY_DOWN)settingsIndex=(settingsIndex+1)%3;if(kd&(KEY_LEFT|KEY_RIGHT|KEY_A)){if(settingsIndex==0){dualScreen=!dualScreen;saveSettings();}else if(settingsIndex==1){phobosFall=!phobosFall;saveSettings();}else mode=MENU;}if(kd&KEY_B)mode=MENU;return;}
 if(mode==GAME){
  if(gameOver){if(kd&KEY_A)newGame();if(kd&KEY_B){mode=MENU;music.start(0);}return;}
  if(paused){if(kd&KEY_UP)pauseIndex=(pauseIndex+2)%3;if(kd&KEY_DOWN)pauseIndex=(pauseIndex+1)%3;if(kd&KEY_A){if(pauseIndex==0){paused=false;audio.setPause(false);}else if(pauseIndex==1)newGame();else{paused=false;mode=MENU;music.start(0);}}if(kd&(KEY_START|KEY_SELECT)){paused=false;audio.setPause(false);}return;}
  if(kd&(KEY_START|KEY_SELECT)){paused=true;pauseIndex=0;audio.setPause(true);return;}if(kd&KEY_LEFT&&fits(curType,curRot,curX-1,curY))curX--;if(kd&KEY_RIGHT&&fits(curType,curRot,curX+1,curY))curX++;if(kh&KEY_DOWN&&frameCounter%3==0){if(fits(curType,curRot,curX,curY+1))curY++;else lockPiece();}if(kd&(KEY_A|KEY_B|KEY_UP)){int nr=(curRot+1)&3;if(fits(curType,nr,curX,curY))curRot=nr;}if(kd&KEY_Y)hardDrop();if(kd&KEY_X)hold();if(kd&(KEY_L|KEY_R))music.playNext();return;
 }
 if(mode==CUTSCENE){if(kd&(KEY_A|KEY_TOUCH)){if(cutsceneStage==100){mode=GAME;music.start(2);}else mode=MENU;}if(kd&(KEY_B|KEY_START)){mode=(cutsceneStage==100)?GAME:MENU;if(mode==GAME)music.start(2);else music.start(0);}return;}
 if(mode==WINNER){if(kd&KEY_LEFT){guardiansRoute=true;phobosRoute=false;}if(kd&KEY_RIGHT){phobosRoute=true;guardiansRoute=false;}if((kd&KEY_A)&&(guardiansRoute||phobosRoute)){mode=GAME;music.start(guardiansRoute?3:4);}return;}
 if(mode==PHOBOS_ROOM){if(kd&KEY_X)roomPose=(roomPose+1)%6;if(kd&KEY_A)roomPose=(roomPose+1)%6;return;}
 if(mode==MATRIX_MODE||mode==PORN_MODE){if(kd&(KEY_A|KEY_B)){mode=returnMode;music.start(mode==GAME?(lines>=100?2:1):0);}return;}
 if(mode==VTD_MODE){if((kh&KEY_L)&&(kh&KEY_R)){mode=returnMode;music.start(mode==GAME?(lines>=100?2:1):0);}return;}
}

static void update(){
 frameCounter++; music.update();
 if((mode==MATRIX_MODE||mode==PORN_MODE)&&secretTimer>0){secretTimer--;if(secretTimer==0){mode=returnMode;music.start(mode==GAME?(lines>=100?2:1):0);}}
 if(mode==GAME&&!paused&&!gameOver&&frameCounter%(std::max(8,35-level*2))==0){if(fits(curType,curRot,curX,curY+1))curY++;else lockPiece();}
}

static void loadArt(){
 bgMenu.load("romfs:/gfx/bg_menu.t3x");phobosMenu.load("romfs:/gfx/phobos_menu_body.t3x");phobosGame.load("romfs:/gfx/phobos_gameplay.t3x");vtdObs.load("romfs:/gfx/vtd_observer.t3x");roomBg.load("romfs:/gfx/phobos_room_bg.t3x");roomFg.load("romfs:/gfx/phobos_room_foreground.t3x");for(int i=0;i<6;i++){char p[80];snprintf(p,sizeof(p),"romfs:/gfx/phobos_room_pose%d.t3x",i);roomPoses[i].load(p);}for(int i=0;i<7;i++)charArt[i].load(charArts[i]);introCastle.load("romfs:/gfx/intro_castle.t3x");introPhobos.load("romfs:/gfx/intro_phobos.t3x");l100Will.load("romfs:/gfx/l100_will.t3x");l100Phobos.load("romfs:/gfx/l100_phobos.t3x");endingHeart.load("romfs:/gfx/ending_heart.t3x");
}
static void freeArt(){bgMenu.free();phobosMenu.free();phobosGame.free();vtdObs.free();roomBg.free();roomFg.free();for(auto& a:roomPoses)a.free();for(auto& a:charArt)a.free();introCastle.free();introPhobos.free();l100Will.free();l100Phobos.free();endingHeart.free();}

int main(){
 srand((unsigned)time(nullptr)); gfxInitDefault(); romfsInit(); cfguInit(); sdmcInit(); C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);C2D_Init(C2D_DEFAULT_MAX_OBJECTS);C2D_Prepare();topTarget=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);botTarget=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);textBuf=C2D_TextBufNew(2048);sysFont=C2D_FontLoadSystem(CFG_REGION_EUR);loadSettings();loadArt();audio.init();music.start(0);
 while(aptMainLoop()&&running){hidScanInput();u32 kd=hidKeysDown(),kh=hidKeysHeld();touchPosition tp;hidTouchRead(&tp);handleInput(kd,kh,tp);update();render();}
 audio.fini();freeArt();C2D_FontFree(sysFont);C2D_TextBufDelete(textBuf);C2D_Fini();C3D_Fini();sdmcExit();cfguExit();romfsExit();gfxExit();return 0;
}
