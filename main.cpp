#include <cmath>
#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <QThread>
#include <QTime>
#include <QDate>
#include <QFile>
#include <QTextStream>
#include <QObject>

#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

// ─── TouchReader ────────────────────────────────────────────────
class TouchReader : public QThread {
    Q_OBJECT
signals:
    void swiped(int dir);           // +1 finger-down, -1 finger-up
    void tappedAt(int x, int y);
    void doubleTappedAt(int x, int y);
protected:
    void run() override {
        int fd = open("/dev/input/event0", O_RDONLY);
        if (fd < 0) return;
        struct input_event ev;
        int sx=-1,sy=-1,cx=-1,cy=-1;
        bool touching=false;
        struct timespec lt={0,0};
        while(true){
            if(read(fd,&ev,sizeof(ev))<(int)sizeof(ev)) continue;
            if(ev.type==EV_ABS){
                if(ev.code==ABS_MT_POSITION_X) cx=ev.value;
                if(ev.code==ABS_MT_POSITION_Y) cy=ev.value;
                if(touching && sx<0 && cx>=0 && cy>=0){ sx=cx; sy=cy; }
            } else if(ev.type==EV_KEY&&ev.code==BTN_TOUCH){
                if(ev.value==1){
                    touching=true;
                    sx=-1; sy=-1;
                } else if(ev.value==0&&touching){
                    touching=false;
                    if(cx>=0&&cy>=0&&sx>=0&&sy>=0){
                        int dx=abs(cx-sx),dy=abs(cy-sy);
                        if(dy>30){
                            emit swiped(cy-sy>0?1:-1);lt={0,0};
                        } else if(dx<30&&dy<30){
                            struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
                            long ms=(now.tv_sec-lt.tv_sec)*1000+(now.tv_nsec-lt.tv_nsec)/1000000;
                            if(ms<400&&lt.tv_sec!=0){emit doubleTappedAt(cx,cy);lt={0,0};}
                            else{emit tappedAt(cx,cy);lt=now;}
                        }
                    }
                    cx=cy=sx=sy=-1;
                }
            }
        }
        close(fd);
    }
};

// ─── AnimState ──────────────────────────────────────────────────
enum class AnimState{Idle,ToWork,Work,FromWork};
static const int WORK_FRAMES=50; // ~10s at 200ms/tick (5fps)

// ─── Settings icons ─────────────────────────────────────────────
static void drawPerson(QPainter&p,QPointF c){
    p.drawEllipse(c+QPointF(0,-22),13.,13.);
    QPainterPath b;b.moveTo(c+QPointF(-18,18));
    b.arcTo(QRectF(c.x()-18,c.y()-2,36,36),180,-180);p.drawPath(b);}
static void drawWifiSm(QPainter&p,QPointF c,float s=1.f){
    for(int i=3;i>=1;i--){float r=i*s*13.f;p.drawArc(QRectF(c.x()-r,c.y()-r,r*2,r*2),30*16,120*16);}
    p.setBrush(p.pen().color());p.setPen(Qt::NoPen);
    p.drawEllipse(c+QPointF(0,3*s),3.5f*s,3.5f*s);
    p.setPen(QPen(p.brush().color(),2.5));p.setBrush(Qt::NoBrush);}
static void drawWifiSmW(QPainter&p,QPointF c){drawWifiSm(p,c);}
static void drawBluetooth(QPainter&p,QPointF c){
    QPainterPath b;
    b.moveTo(c+QPointF(-12,-12));b.lineTo(c+QPointF(10,6));
    b.lineTo(c+QPointF(-12,20));b.moveTo(c+QPointF(10,-6));
    b.lineTo(c+QPointF(-12,8));b.moveTo(c+QPointF(0,-22));b.lineTo(c+QPointF(0,22));
    p.drawPath(b);}
static void drawSliders(QPainter&p,QPointF c){
    int off[3]={-8,4,-4};
    for(int i=0;i<3;i++){float x=c.x()-18+i*18;
        p.drawLine(QPointF(x,c.y()-18),QPointF(x,c.y()+18));
        float hy=c.y()+off[i];p.drawLine(QPointF(x-7,hy),QPointF(x+7,hy));
        p.setBrush(QColor(30,30,30));p.setPen(Qt::NoPen);p.drawEllipse(QPointF(x,hy),4.5,4.5);
        p.setPen(QPen(QColor(255,255,255),2.5));p.setBrush(Qt::NoBrush);}}
static void drawWaveform(QPainter&p,QPointF c){
    QPainterPath w;w.moveTo(c+QPointF(-30,0));w.lineTo(c+QPointF(-15,0));
    w.lineTo(c+QPointF(-8,-20));w.lineTo(c+QPointF(0,18));
    w.lineTo(c+QPointF(8,-12));w.lineTo(c+QPointF(15,0));w.lineTo(c+QPointF(30,0));p.drawPath(w);}
static void drawRefresh(QPainter&p,QPointF c){
    float r=17;
    p.drawArc(QRectF(c.x()-r,c.y()-r,r*2,r*2),40*16,140*16);
    p.drawArc(QRectF(c.x()-r,c.y()-r,r*2,r*2),220*16,140*16);
    auto arr=[&](float a,float d){float ag=a*M_PI/180.f;
        QPointF t(c.x()+r*cos(ag),c.y()-r*sin(ag));
        float a2=(a+d*30)*M_PI/180.f;
        p.drawLine(t,QPointF(c.x()+(r-8)*cos(a2),c.y()-(r-8)*sin(a2)));};
    arr(180,-1);arr(0,1);}

// ─── Skills ─────────────────────────────────────────────────────
static const int SKILL_COUNT=9;
struct Skill{QString name;int price;bool owned;};
static const QString COIN_FILE ="/home/bianbu/.dsi_coins";
static const QString SKILL_FILE="/home/bianbu/.dsi_skills";

// ─── Layout constants ────────────────────────────────────────────
static const int HDR_H=48, DOT_H=28, BAR_H=72;
static const int MX=16, GX=12, GY=10;

// ─── SwipeScreen ────────────────────────────────────────────────
class SwipeScreen : public QWidget {
    Q_OBJECT
public:
    SwipeScreen()
        : current(0),frame(0),workCount(0),animState(AnimState::Idle)
        , cpuUsage(0),prevCpuTotal(0),prevCpuIdle(0)
        , totalCoins(0),todayEarned(0)
        , selectedSkill(-1),skillScrollRow(0)
    {
        sheets[0].load("./dragon-new/idle.png");
        sheets[1].load("./dragon-new/idle2work.png");
        sheets[2].load("./dragon-new/work.png");

        skills[0]={"技能 1", 3,false};skills[1]={"技能 2", 5,false};
        skills[2]={"技能 3", 8,false};skills[3]={"技能 4",10,false};
        skills[4]={"技能 5",12,false};skills[5]={"技能 6",15,false};
        skills[6]={"技能 7", 6,false};skills[7]={"技能 8", 9,false};
        skills[8]={"技能 9",20,false};

        animTimer=new QTimer(this);animTimer->setInterval(143);
        connect(animTimer,&QTimer::timeout,this,&SwipeScreen::nextFrame);
        animTimer->start();

        sysTimer=new QTimer(this);sysTimer->setInterval(1000);
        connect(sysTimer,&QTimer::timeout,this,&SwipeScreen::updateSysInfo);
        sysTimer->start();updateSysInfo();

        msgTimer=new QTimer(this);msgTimer->setSingleShot(true);
        connect(msgTimer,&QTimer::timeout,this,[this]{coinMsg.clear();update();});

        loadCoins();loadSkills();
        setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
        showFullScreen();

        TouchReader*r=new TouchReader();
        connect(r,&TouchReader::swiped,        this,&SwipeScreen::onSwipe);
        connect(r,&TouchReader::tappedAt,      this,&SwipeScreen::onTap);
        connect(r,&TouchReader::doubleTappedAt,this,&SwipeScreen::onDoubleTap);
        r->start();
    }

public slots:
    void onSwipe(int dir){
        if(current==2){
            int maxRow=((SKILL_COUNT+2)/3)-2;
            if(dir==-1) skillScrollRow=qMin(skillScrollRow+1,maxRow);
            else         skillScrollRow=qMax(skillScrollRow-1,0);
            update(); return;
        }
        current=(current+(dir>0?1:2))%3;
        if(current==1){animState=AnimState::Idle;frame=0;animTimer->setInterval(143);}
        update();
    }

    void onTap(int x,int y){
        if(y>=height()-DOT_H){
            int dotR=7,spacing=24,totalW=3*dotR*2+2*spacing;
            int bx=(width()-totalW)/2;
            for(int i=0;i<3;i++){
                int dx=bx+i*(dotR*2+spacing);
                if(x>=dx-10&&x<=dx+dotR*2+10){
                    current=i;
                    if(current==1){animState=AnimState::Idle;frame=0;animTimer->setInterval(143);}
                    update();return;
                }
            }
        }
        if(current==1){
            if(x>=250&&x<=550&&y>=90&&y<=390){
                earnCoins(1);
            }
        } else if(current==2){
            handleSkillTap(x,y);
        }
        update();
    }

    void onDoubleTap(int x,int y){
        if(current==1&&x>=250&&x<=550&&y>=90&&y<=390){
            earnCoins(2);
            if(animState==AnimState::Idle)
            {animState=AnimState::ToWork;frame=0;workCount=0;animTimer->setInterval(200);}
        }
        update();
    }

    void nextFrame(){
        if(current!=1)return;
        switch(animState){
        case AnimState::Idle:     frame=(frame+1)%20;break;
        case AnimState::ToWork:   if(++frame>=8){animState=AnimState::Work;frame=0;workCount=0;}break;
        case AnimState::Work:
            frame=(frame+1)%10;
            if(++workCount>=WORK_FRAMES){animState=AnimState::FromWork;frame=7;}break;
        case AnimState::FromWork: if(--frame<0){animState=AnimState::Idle;frame=0;animTimer->setInterval(143);}break;
        }
        update();
    }

    void updateSysInfo(){
        currentTime=QTime::currentTime().toString("HH:mm");
        QFile f("/proc/stat");
        if(f.open(QIODevice::ReadOnly)){
            QString line=f.readLine();f.close();
            QStringList ps=line.split(' ',Qt::SkipEmptyParts);
            if(ps.size()>=5){
                long u=ps[1].toLong(),n=ps[2].toLong(),s=ps[3].toLong(),
                     id=ps[4].toLong(),iw=ps.size()>5?ps[5].toLong():0;
                long tot=u+n+s+id+iw,dt=tot-prevCpuTotal,di=id-prevCpuIdle;
                if(dt>0)cpuUsage=(dt-di)*100/dt;
                prevCpuTotal=tot;prevCpuIdle=id;
            }
        }
        if(current==0||current==2)update();
    }

protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        switch(current){case 0:paintSettings(p);break;case 1:paintDragon(p);break;case 2:paintSkills(p);break;}
    }

private:
    // ── Layout helpers ──────────────────────────────────────────
    int tileH() const {
        int gridH=height()-HDR_H-8-GY-BAR_H-DOT_H;
        return (gridH-GY)/2;
    }
    int tileW() const { return (width()-2*MX-2*GX)/3; }

    QRect tileRect(int idx, int scrollRow) const {
        int col=idx%3, row=idx/3;
        int x=MX+col*(tileW()+GX);
        int y=HDR_H+8+(row-scrollRow)*(tileH()+GY);
        return QRect(x,y,tileW(),tileH());
    }
    QRect buyBtnRect() const {
        int barTop=height()-DOT_H-BAR_H;
        return QRect(width()-190,barTop+12,170,BAR_H-24);
    }
    int gridTop()   const { return HDR_H+8; }
    int gridBottom()const { return height()-DOT_H-BAR_H; }

    // ── Coin ────────────────────────────────────────────────────
    void loadCoins(){
        QFile f(COIN_FILE);if(!f.open(QIODevice::ReadOnly))return;
        QTextStream in(&f);QString date;
        in>>totalCoins>>todayEarned>>date;
        if(date!=QDate::currentDate().toString("yyyy-MM-dd"))todayEarned=0;
        lastDate=QDate::currentDate().toString("yyyy-MM-dd");
    }
    void saveCoins(){
        QFile f(COIN_FILE);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate))return;
        QTextStream out(&f);
        out<<totalCoins<<" "<<todayEarned<<" "<<QDate::currentDate().toString("yyyy-MM-dd");
    }
    void earnCoins(int amount){
        QString today=QDate::currentDate().toString("yyyy-MM-dd");
        if(today!=lastDate){todayEarned=0;lastDate=today;}
        if(todayEarned>=10){coinMsg="当前获得金币已达上限";msgTimer->start(2000);update();return;}
        int earn=qMin(amount,10-todayEarned);
        totalCoins+=earn;todayEarned+=earn;saveCoins();
        if(todayEarned>=10){coinMsg="当前获得金币已达上限";msgTimer->start(2000);}
        update();
    }
    void drawCoinBadge(QPainter&p,int right,int cy){
        int r=13,cx=right-r-6;
        p.setPen(Qt::NoPen);p.setBrush(QColor(255,215,0));
        p.drawEllipse(QPointF(cx,cy),(double)r,(double)r);
        p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(184,134,11),2));
        p.drawEllipse(QPointF(cx,cy),(double)(r-4),(double)(r-4));
        QFont cf;cf.setPixelSize(15);cf.setBold(true);p.setFont(cf);p.setPen(Qt::white);
        p.drawText(QRect(cx-r-52,cy-11,48,22),Qt::AlignVCenter|Qt::AlignRight,
                   QString::number(totalCoins));
    }

    // ── Skills ──────────────────────────────────────────────────
    void loadSkills(){
        QFile f(SKILL_FILE);if(!f.open(QIODevice::ReadOnly))return;
        QTextStream in(&f);for(int i=0;i<SKILL_COUNT;i++){int v=0;in>>v;skills[i].owned=(v==1);}
    }
    void saveSkills(){
        QFile f(SKILL_FILE);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate))return;
        QTextStream out(&f);for(int i=0;i<SKILL_COUNT;i++)out<<(skills[i].owned?1:0)<<(i<SKILL_COUNT-1?" ":"");
    }
    void handleSkillTap(int x,int y){
        if(selectedSkill>=0&&buyBtnRect().contains(x,y)){
            Skill&sk=skills[selectedSkill];
            if(sk.owned){coinMsg="已拥有该技能";msgTimer->start(2000);return;}
            if(totalCoins<sk.price){coinMsg="金币不足";msgTimer->start(2000);return;}
            totalCoins-=sk.price;sk.owned=true;saveCoins();saveSkills();
            coinMsg="购买成功！";msgTimer->start(2000);return;
        }
        if(y<gridTop()||y>gridBottom())return;
        for(int i=0;i<SKILL_COUNT;i++){
            if(tileRect(i,skillScrollRow).contains(x,y)){
                selectedSkill=(selectedSkill==i)?-1:i;
                coinMsg.clear();return;
            }
        }
    }

    void drawImagePlaceholder(QPainter&p,QRect r,bool owned){
        QColor fg=owned?QColor(200,200,200):QColor(90,90,90);
        p.setPen(QPen(fg,1.5));p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r,5,5);
        QPainterPath mp;
        mp.moveTo(r.left()+5,r.bottom()-7);
        mp.lineTo(r.left()+r.width()*0.35f,r.top()+r.height()*0.45f);
        mp.lineTo(r.left()+r.width()*0.62f,r.top()+r.height()*0.65f);
        mp.lineTo(r.right()-5,r.bottom()-7);
        p.drawPath(mp);
        p.drawEllipse(QPointF(r.right()-10,r.top()+9),4,4);
    }

    // ── Skills page ─────────────────────────────────────────────
    void paintSkills(QPainter&p){
        p.fillRect(rect(),Qt::black);

        QFont hf;hf.setPixelSize(17);p.setFont(hf);p.setPen(QColor(200,200,200));
        p.drawText(QRect(MX,0,400,HDR_H),Qt::AlignVCenter|Qt::AlignLeft,
                   QString("技能套餐    CPU %1%").arg(cpuUsage));
        p.drawText(QRect(width()-180,0,80,HDR_H),Qt::AlignVCenter|Qt::AlignRight,currentTime);
        drawCoinBadge(p,width()-8,HDR_H/2);
        p.setPen(QColor(40,40,40));p.drawLine(0,HDR_H,width(),HDR_H);

        int totalRows=(SKILL_COUNT+2)/3;
        if(totalRows>2){
            int stripH=gridBottom()-gridTop();
            int trackH=stripH*2/totalRows;
            int trackY=gridTop()+stripH*skillScrollRow/totalRows;
            p.setPen(Qt::NoPen);p.setBrush(QColor(60,60,60));
            p.drawRoundedRect(QRect(width()-6,gridTop(),4,stripH),2,2);
            p.setBrush(QColor(160,160,160));
            p.drawRoundedRect(QRect(width()-6,trackY,4,trackH),2,2);
        }

        p.setClipRect(0,gridTop(),width(),gridBottom()-gridTop());

        for(int i=0;i<SKILL_COUNT;i++){
            QRect tile=tileRect(i,skillScrollRow);
            if(tile.bottom()<gridTop()||tile.top()>gridBottom())continue;

            bool owned=skills[i].owned,sel=(selectedSkill==i);
            p.setPen(Qt::NoPen);p.setBrush(owned?QColor(36,36,40):QColor(22,22,24));
            p.drawRoundedRect(tile,12,12);
            if(sel){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(255,215,0),2));
                p.drawRoundedRect(tile.adjusted(1,1,-1,-1),11,11);}

            int imgSz=qMin(tileH()-18,58);
            int imgY=tile.top()+(tile.height()-imgSz)/2;
            drawImagePlaceholder(p,QRect(tile.left()+8,imgY,imgSz,imgSz),owned);

            int txtX=tile.left()+imgSz+18,txtW=tile.right()-txtX-8;
            QFont nf;nf.setPixelSize(14);nf.setBold(true);p.setFont(nf);
            p.setPen(owned?QColor(225,225,225):QColor(120,120,120));
            p.drawText(QRect(txtX,tile.top()+tile.height()/2-20,txtW,22),Qt::AlignLeft|Qt::AlignVCenter,skills[i].name);
            QFont pf;pf.setPixelSize(12);p.setFont(pf);
            p.setPen(owned?QColor(255,215,0):QColor(95,95,95));
            p.drawText(QRect(txtX,tile.top()+tile.height()/2+3,txtW,20),Qt::AlignLeft|Qt::AlignVCenter,
                       owned?"已拥有":QString("%1 金币").arg(skills[i].price));
            if(owned){p.setPen(QPen(QColor(80,200,120),2.5));
                QPointF cc(tile.right()-13,tile.top()+13);
                p.drawLine(cc+QPointF(-6,0),cc+QPointF(-2,5));
                p.drawLine(cc+QPointF(-2,5),cc+QPointF(7,-6));}
        }
        p.setClipping(false);

        int barTop=height()-DOT_H-BAR_H;
        p.setPen(QColor(50,50,50));p.drawLine(0,barTop,width(),barTop);
        p.fillRect(QRect(0,barTop,width(),BAR_H),QColor(14,14,16));

        if(selectedSkill>=0){
            Skill&sk=skills[selectedSkill];
            QFont inf;inf.setPixelSize(15);p.setFont(inf);p.setPen(QColor(210,210,210));
            QString info=sk.name+"  ";
            p.drawText(QRect(MX,barTop,width()-200,BAR_H),Qt::AlignVCenter|Qt::AlignLeft,info);
            int nameW=p.fontMetrics().horizontalAdvance(info);
            p.setPen(sk.owned?QColor(80,200,120):QColor(255,215,0));
            p.drawText(QRect(MX+nameW,barTop,width()-200-nameW,BAR_H),Qt::AlignVCenter|Qt::AlignLeft,
                       sk.owned?"已拥有":QString("%1 金币").arg(sk.price));

            QRect btn=buyBtnRect();
            bool canBuy=!sk.owned&&totalCoins>=sk.price;
            p.setPen(Qt::NoPen);
            p.setBrush(sk.owned?QColor(45,45,50):canBuy?QColor(255,215,0):QColor(55,45,25));
            p.drawRoundedRect(btn,10,10);
            QFont bf;bf.setPixelSize(15);bf.setBold(true);p.setFont(bf);
            p.setPen(sk.owned||!canBuy?QColor(110,110,110):Qt::black);
            p.drawText(btn,Qt::AlignCenter,sk.owned?"已拥有":"点击购买");
        } else {
            QFont inf;inf.setPixelSize(13);p.setFont(inf);p.setPen(QColor(70,70,70));
            p.drawText(QRect(0,barTop,width(),BAR_H),Qt::AlignCenter,"选择一个技能查看详情");
        }

        if(!coinMsg.isEmpty()){
            QFont mf;mf.setPixelSize(13);p.setFont(mf);
            QRect mr(0,barTop-30,width(),24);
            p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,180));
            p.drawRoundedRect(mr.adjusted(width()/2-120,-2,-(width()/2-120),2),8,8);
            p.setPen(QColor(255,220,50));p.drawText(mr,Qt::AlignCenter,coinMsg);
        }
        paintDots(p);
    }

    // ── Dragon page ─────────────────────────────────────────────
    void paintDragon(QPainter&p){
        p.fillRect(rect(),Qt::black);
        int si=0;
        bool isTransition=(animState==AnimState::ToWork||animState==AnimState::FromWork);
        switch(animState){
        case AnimState::Idle:    si=0;break;
        case AnimState::ToWork:
        case AnimState::FromWork:si=1;break;
        case AnimState::Work:    si=2;break;
        }
        if(!sheets[si].isNull()){
            int fw=isTransition?350:300;
            int fh=380;
            QPixmap px=sheets[si].copy(frame*fw,0,fw,fh);
            p.drawPixmap((width()-fw)/2,(height()-fh)/2,px);
        }
        drawCoinBadge(p,width()-8,24);
        if(!coinMsg.isEmpty()){
            QFont mf;mf.setPixelSize(13);p.setFont(mf);
            QRect mr(0,height()-60,width(),24);
            p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,160));
            p.drawRoundedRect(mr.adjusted(width()/2-110,-4,-(width()/2-110),4),8,8);
            p.setPen(QColor(255,220,50));p.drawText(mr,Qt::AlignCenter,coinMsg);
        }
        paintDots(p);
    }

    // ── Settings page ────────────────────────────────────────────
    void paintSettings(QPainter&p){
        p.fillRect(rect(),Qt::black);
        QFont hf;hf.setPixelSize(17);p.setFont(hf);p.setPen(QColor(200,200,200));
        p.drawText(QRect(MX,0,400,HDR_H),Qt::AlignVCenter|Qt::AlignLeft,
                   QString("设置    CPU %1%").arg(cpuUsage));
        p.drawText(QRect(width()-160,0,100,HDR_H),Qt::AlignVCenter|Qt::AlignRight,currentTime);
        p.setPen(QPen(QColor(200,200,200),2));
        QPointF wc(width()-28,HDR_H/2);
        for(int i=2;i>=1;i--){float r=i*7.f;p.drawArc(QRectF(wc.x()-r,wc.y()-r,r*2,r*2),30*16,120*16);}
        p.setBrush(QColor(200,200,200));p.setPen(Qt::NoPen);p.drawEllipse(wc+QPointF(0,3),2.5,2.5);
        p.setPen(QPen(QColor(200,200,200),2));p.setBrush(Qt::NoBrush);
        p.setPen(QColor(40,40,40));p.drawLine(0,HDR_H,width(),HDR_H);

        const int COLS=3,ROWS=2,my=8,gx=12,gy=12,dotsH=30;
        int gridTop2=HDR_H+my,gridH=height()-HDR_H-dotsH-my*2;
        int tw=(width()-2*MX-(COLS-1)*gx)/COLS,th=(gridH-(ROWS-1)*gy)/ROWS;
        struct{const char*l;void(*d)(QPainter&,QPointF);}items[6]={
            {"个人中心",drawPerson},{"WiFi",drawWifiSmW},{"蓝牙",drawBluetooth},
            {"通用设置",drawSliders},{"频道配置",drawWaveform},{"频度查询",drawRefresh}};
        for(int i=0;i<6;i++){
            int col=i%3,row=i/3,tx=MX+col*(tw+gx),ty=gridTop2+row*(th+gy);
            QRect tile(tx,ty,tw,th);
            p.setPen(Qt::NoPen);p.setBrush(QColor(28,28,30));p.drawRoundedRect(tile,16,16);
            QPointF ic(tx+tw/2.,ty+th/2.-14);
            p.setPen(QPen(QColor(230,230,230),2.5));p.setBrush(Qt::NoBrush);
            items[i].d(p,ic);
            QFont lf;lf.setPixelSize(13);p.setFont(lf);p.setPen(QColor(200,200,200));
            p.drawText(QRect(tx,ty+th-32,tw,26),Qt::AlignHCenter|Qt::AlignVCenter,items[i].l);
        }
        paintDots(p);
    }

    // ── Dots ────────────────────────────────────────────────────
    void paintDots(QPainter&p){
        int dotR=7,spacing=24,totalW=3*dotR*2+2*spacing;
        int bx=(width()-totalW)/2,by=height()-20;
        for(int i=0;i<3;i++){
            p.setBrush(i==current?Qt::white:QColor(255,255,255,70));
            p.setPen(Qt::NoPen);
            p.drawEllipse(bx+i*(dotR*2+spacing),by,dotR*2,dotR*2);
        }
    }

    // ── Members ─────────────────────────────────────────────────
    QPixmap sheets[3];
    Skill   skills[SKILL_COUNT];
    int     current,frame,workCount;
    AnimState animState;
    QTimer  *animTimer,*sysTimer,*msgTimer;
    int     cpuUsage;
    long    prevCpuTotal,prevCpuIdle;
    QString currentTime,lastDate,coinMsg;
    int     totalCoins,todayEarned;
    int     selectedSkill,skillScrollRow;
};

#include "main.moc"
int main(int argc,char*argv[]){QApplication app(argc,argv);SwipeScreen w;return app.exec();}
