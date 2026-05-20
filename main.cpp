#include <cmath>
#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <QThread>
#include <QTime>
#include <QFile>
#include <QObject>

#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

// ───────────────────────── TouchReader ──────────────────────────
class TouchReader : public QThread {
    Q_OBJECT
signals:
    void swiped(int dir);
    void tapped();
    void doubleTapped();

protected:
    void run() override {
        int fd = open("/dev/input/event0", O_RDONLY);
        if (fd < 0) return;

        struct input_event ev;
        int startX=-1, startY=-1, curX=-1, curY=-1;
        bool touching = false;
        struct timespec lastTap = {0,0};

        while (true) {
            if (read(fd, &ev, sizeof(ev)) < (int)sizeof(ev)) continue;
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_MT_POSITION_X) curX = ev.value;
                if (ev.code == ABS_MT_POSITION_Y) curY = ev.value;
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                if (ev.value == 1) {
                    touching = true;
                    startX = (curX >= 0) ? curX : -1;
                    startY = (curY >= 0) ? curY : -1;
                } else if (ev.value == 0 && touching) {
                    touching = false;
                    if (curX < 0 || curY < 0 || startX < 0 || startY < 0) {
                        startX = startY = -1; continue;
                    }
                    int dx = abs(curX - startX), dy = abs(curY - startY);
                    if (dx < 30 && dy < 30) {
                        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
                        long ms = (now.tv_sec - lastTap.tv_sec)*1000
                                + (now.tv_nsec - lastTap.tv_nsec)/1000000;
                        bool inCenter = curX>=250&&curX<=550&&curY>=90&&curY<=390;
                        if (ms < 400 && lastTap.tv_sec != 0) {
                            if (inCenter) emit doubleTapped();
                            lastTap = {0,0};
                        } else {
                            if (inCenter) emit tapped();
                            lastTap = now;
                        }
                    } else if (dy > 30) {
                        emit swiped(curY - startY > 0 ? 1 : -1);
                        lastTap = {0,0};
                    }
                    startX = startY = -1;
                }
            }
        }
        close(fd);
    }
};

// ───────────────────────── AnimState ────────────────────────────
enum class AnimState { Idle, ToHeart, Heart, FromHeart,
                              ToWork,  Work,  FromWork };
static const int WORK_TOTAL_FRAMES = 15 * 7;

// ───────────────────────── Icon painters ────────────────────────
static void drawPerson(QPainter &p, QPointF c) {
    p.drawEllipse(c + QPointF(0, -22), 13.0, 13.0);
    QPainterPath body;
    body.moveTo(c + QPointF(-18, 18));
    body.arcTo(QRectF(c.x()-18, c.y()-2, 36, 36), 180, -180);
    p.drawPath(body);
}
static void drawWifi(QPainter &p, QPointF c) {
    for (int i = 3; i >= 1; i--) {
        float r = i * 13.0f;
        p.drawArc(QRectF(c.x()-r, c.y()-r, r*2, r*2), 30*16, 120*16);
    }
    p.setBrush(p.pen().color()); p.setPen(Qt::NoPen);
    p.drawEllipse(c + QPointF(0,3), 3.5, 3.5);
    p.setPen(QPen(QColor(255,255,255), 2.5)); p.setBrush(Qt::NoBrush);
}
static void drawBluetooth(QPainter &p, QPointF c) {
    QPainterPath bp;
    bp.moveTo(c + QPointF(-12, -12));
    bp.lineTo(c + QPointF(10,   6));
    bp.lineTo(c + QPointF(-12,  20));
    bp.moveTo(c + QPointF(10,  -6));
    bp.lineTo(c + QPointF(-12,  8));
    bp.moveTo(c + QPointF(0,  -22));
    bp.lineTo(c + QPointF(0,   22));
    p.drawPath(bp);
}
static void drawSliders(QPainter &p, QPointF c) {
    int offsets[3] = {-8, 4, -4};
    for (int i = 0; i < 3; i++) {
        float x = c.x() - 18 + i * 18;
        p.drawLine(QPointF(x, c.y()-18), QPointF(x, c.y()+18));
        float hy = c.y() + offsets[i];
        p.drawLine(QPointF(x-7, hy), QPointF(x+7, hy));
        p.setBrush(QColor(30,30,30)); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(x, hy), 4.5, 4.5);
        p.setPen(QPen(QColor(255,255,255), 2.5)); p.setBrush(Qt::NoBrush);
    }
}
static void drawWaveform(QPainter &p, QPointF c) {
    QPainterPath wp;
    wp.moveTo(c + QPointF(-30,  0));
    wp.lineTo(c + QPointF(-15,  0));
    wp.lineTo(c + QPointF( -8,-20));
    wp.lineTo(c + QPointF(  0, 18));
    wp.lineTo(c + QPointF(  8,-12));
    wp.lineTo(c + QPointF( 15,  0));
    wp.lineTo(c + QPointF( 30,  0));
    p.drawPath(wp);
}
static void drawRefresh(QPainter &p, QPointF c) {
    float r = 17;
    p.drawArc(QRectF(c.x()-r, c.y()-r, r*2, r*2), 40*16, 140*16);
    p.drawArc(QRectF(c.x()-r, c.y()-r, r*2, r*2), 220*16, 140*16);
    // arrow heads
    auto arrow = [&](float angDeg, float dir) {
        float ang = angDeg * M_PI / 180.0f;
        QPointF tip(c.x() + r*cos(ang), c.y() - r*sin(ang));
        float a2 = (angDeg + dir*30) * M_PI / 180.0f;
        QPointF a(c.x() + (r-8)*cos(a2), c.y() - (r-8)*sin(a2));
        float a3 = (angDeg + dir*30) * M_PI / 180.0f;
        QPointF b(c.x() + (r+8)*cos(a3), c.y() - (r+8)*sin(a3));
        (void)b;
        p.drawLine(tip, a);
    };
    arrow(180, -1); arrow(0, 1);
}

// ───────────────────────── SwipeScreen ──────────────────────────
class SwipeScreen : public QWidget {
    Q_OBJECT
public:
    SwipeScreen() : current(0), frame(0), workCount(0),
                    animState(AnimState::Idle), cpuUsage(0),
                    prevCpuTotal(0), prevCpuIdle(0) {

        // Animation spritesheets
        sheets[0].load("/home/bianbu/Desktop/dragon/idle.png");
        sheets[1].load("/home/bianbu/Desktop/dragon/idle2heart.png");
        sheets[2].load("/home/bianbu/Desktop/dragon/heart.png");
        sheets[3].load("/home/bianbu/Desktop/dragon/idle2work.png");
        sheets[4].load("/home/bianbu/Desktop/dragon/work.png");

        // Animation timer 7fps
        animTimer = new QTimer(this);
        animTimer->setInterval(143);
        connect(animTimer, &QTimer::timeout, this, &SwipeScreen::nextFrame);
        animTimer->start();

        // System info timer 1s
        sysTimer = new QTimer(this);
        sysTimer->setInterval(1000);
        connect(sysTimer, &QTimer::timeout, this, &SwipeScreen::updateSysInfo);
        sysTimer->start();
        updateSysInfo();

        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        showFullScreen();

        TouchReader *reader = new TouchReader();
        connect(reader, &TouchReader::swiped,       this, &SwipeScreen::onSwipe);
        connect(reader, &TouchReader::tapped,       this, &SwipeScreen::onTap);
        connect(reader, &TouchReader::doubleTapped, this, &SwipeScreen::onDoubleTap);
        reader->start();
    }

public slots:
    void onSwipe(int dir) {
        current = (current + (dir > 0 ? 1 : 2)) % 3;
        if (current == 1) { animState = AnimState::Idle; frame = 0; }
        update();
    }
    void onTap() {
        if (current != 1 || animState != AnimState::Idle) return;
        animState = AnimState::ToHeart; frame = 0; update();
    }
    void onDoubleTap() {
        if (current != 1) return;
        if (animState==AnimState::ToWork||animState==AnimState::Work||
            animState==AnimState::FromWork) return;
        animState = AnimState::ToWork; frame = 0; workCount = 0; update();
    }
    void nextFrame() {
        if (current != 1) return;
        switch (animState) {
        case AnimState::Idle:     frame = (frame+1)%18; break;
        case AnimState::ToHeart:  if(++frame>=7){animState=AnimState::Heart;frame=0;} break;
        case AnimState::Heart:    if(++frame>=17){animState=AnimState::FromHeart;frame=6;} break;
        case AnimState::FromHeart:if(--frame<0){animState=AnimState::Idle;frame=0;} break;
        case AnimState::ToWork:   if(++frame>=8){animState=AnimState::Work;frame=0;workCount=0;} break;
        case AnimState::Work:
            frame=(frame+1)%11;
            if(++workCount>=WORK_TOTAL_FRAMES){animState=AnimState::FromWork;frame=7;}
            break;
        case AnimState::FromWork: if(--frame<0){animState=AnimState::Idle;frame=0;} break;
        }
        update();
    }
    void updateSysInfo() {
        currentTime = QTime::currentTime().toString("HH:mm");
        QFile f("/proc/stat");
        if (f.open(QIODevice::ReadOnly)) {
            QString line = f.readLine();
            f.close();
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 5) {
                long user=parts[1].toLong(), nice=parts[2].toLong(),
                     sys=parts[3].toLong(), idle=parts[4].toLong(),
                     iow=parts.size()>5?parts[5].toLong():0;
                long total = user+nice+sys+idle+iow;
                long dt = total - prevCpuTotal, di = idle - prevCpuIdle;
                if (dt > 0) cpuUsage = (dt-di)*100/dt;
                prevCpuTotal = total; prevCpuIdle = idle;
            }
        }
        if (current == 0) update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (current == 0) paintSettings(p);
        else if (current == 1) paintDragon(p);
        else { p.fillRect(rect(), QColor(180,200,255)); paintDots(p); }
    }

private:
    // ── Settings page ──────────────────────────────────────────
    void paintSettings(QPainter &p) {
        p.fillRect(rect(), Qt::black);

        // ── Header ──
        QFont hf; hf.setPixelSize(17);
        p.setFont(hf); p.setPen(QColor(200,200,200));
        p.drawText(QRect(18,0,400,46), Qt::AlignVCenter|Qt::AlignLeft,
                   QString("设置    CPU %1%").arg(cpuUsage));

        // time
        QFont tf; tf.setPixelSize(17); tf.setBold(false);
        p.setFont(tf);
        p.drawText(QRect(width()-160,0,100,46), Qt::AlignVCenter|Qt::AlignRight,
                   currentTime);

        // small wifi icon in header
        p.setPen(QPen(QColor(200,200,200), 2.0));
        QPointF wc(width()-28, 23);
        for (int i=2; i>=1; i--) {
            float r = i*7.0f;
            p.drawArc(QRectF(wc.x()-r,wc.y()-r,r*2,r*2), 30*16, 120*16);
        }
        p.setBrush(QColor(200,200,200)); p.setPen(Qt::NoPen);
        p.drawEllipse(wc+QPointF(0,3), 2.5, 2.5);
        p.setPen(QPen(QColor(200,200,200), 2.0)); p.setBrush(Qt::NoBrush);

        // divider
        p.setPen(QColor(40,40,40));
        p.drawLine(0, 46, width(), 46);

        // ── Grid ──
        const int COLS=3, ROWS=2;
        int mx=18, my=8, gx=12, gy=12;
        int headerH=48, dotsH=30;
        int gridTop = headerH+my;
        int gridH   = height()-headerH-dotsH-my*2;
        int tileW   = (width()-2*mx-(COLS-1)*gx)/COLS;
        int tileH   = (gridH-(ROWS-1)*gy)/ROWS;

        struct { const char *label; void(*draw)(QPainter&,QPointF); } items[6] = {
            {"个人中心", drawPerson},
            {"WiFi",    drawWifi},
            {"蓝牙",    drawBluetooth},
            {"通用设置", drawSliders},
            {"频道配置", drawWaveform},
            {"频度查询", drawRefresh},
        };

        for (int i=0; i<6; i++) {
            int col=i%3, row=i/3;
            int tx = mx + col*(tileW+gx);
            int ty = gridTop + row*(tileH+gy);
            QRect tile(tx, ty, tileW, tileH);

            // card bg
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(28,28,30));
            p.drawRoundedRect(tile, 16, 16);

            // icon
            QPointF ic(tx+tileW/2.0, ty+tileH/2.0-14);
            p.setPen(QPen(QColor(230,230,230), 2.5));
            p.setBrush(Qt::NoBrush);
            items[i].draw(p, ic);

            // label
            QFont lf; lf.setPixelSize(13);
            p.setFont(lf); p.setPen(QColor(200,200,200));
            p.drawText(QRect(tx, ty+tileH-32, tileW, 26),
                       Qt::AlignHCenter|Qt::AlignVCenter, items[i].label);
        }

        paintDots(p);
    }

    // ── Dragon page ─────────────────────────────────────────────
    void paintDragon(QPainter &p) {
        p.fillRect(rect(), Qt::black);
        int sheetIdx = 0;
        switch (animState) {
        case AnimState::Idle:                               sheetIdx=0; break;
        case AnimState::ToHeart: case AnimState::FromHeart: sheetIdx=1; break;
        case AnimState::Heart:                              sheetIdx=2; break;
        case AnimState::ToWork:  case AnimState::FromWork:  sheetIdx=3; break;
        case AnimState::Work:                               sheetIdx=4; break;
        }
        QPixmap &sheet = sheets[sheetIdx];
        if (!sheet.isNull()) {
            QPixmap px = sheet.copy(frame*300,0,300,380);
            p.drawPixmap((width()-300)/2,(height()-380)/2,px);
        }
        paintDots(p);
    }

    // ── Page indicator dots ──────────────────────────────────────
    void paintDots(QPainter &p) {
        int dotR=7, spacing=24;
        int totalW = 3*dotR*2 + 2*spacing;
        int bx=(width()-totalW)/2, by=height()-22;
        for (int i=0; i<3; i++) {
            p.setBrush(i==current ? Qt::white : QColor(255,255,255,70));
            p.setPen(Qt::NoPen);
            p.drawEllipse(bx+i*(dotR*2+spacing), by, dotR*2, dotR*2);
        }
    }

    QPixmap sheets[5];
    int current, frame, workCount;
    AnimState animState;
    QTimer *animTimer, *sysTimer;
    int cpuUsage;
    long prevCpuTotal, prevCpuIdle;
    QString currentTime;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    SwipeScreen w;
    return app.exec();
}
