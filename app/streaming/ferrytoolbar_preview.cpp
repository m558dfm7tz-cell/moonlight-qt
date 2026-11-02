#include "ferrytoolbar_preview.h"
#include "ferrytoolbar.h"
#include "ferrytoolbar_geometry.h"
#include "SDL_compat.h"
#include <SDL_ttf.h>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <cstdio>

int runFerryToolbarPreview(const QString& outDir)
{
    QDir().mkpath(outDir);
    if(TTF_Init()!=0)return 1;
    SDL_Window* stream=SDL_CreateWindow("FerryOffscreenFixture",100,100,1100,760,SDL_WINDOW_BORDERLESS|SDL_WINDOW_HIDDEN);
    if(!stream)return 2;
    FerryToolbar tb(nullptr);
    if(!tb.init(stream,true))return 3;
    tb.setQuality("1080",60,20,false);
    QJsonArray metrics;
    auto save=[&](QString name,FerryToolbar::DockEdge edge,bool expanded,bool privacy,bool hostAudio,bool muted,bool quality){
        tb.debugSetState(edge,expanded,privacy,hostAudio,muted,quality,-1);
        SDL_Delay(160);
        tb.debugSetState(edge,expanded,privacy,hostAudio,muted,quality,-1);
        QImage component=tb.debugSnapshot();
        QImage frame(component.width()+40,component.height()+40,QImage::Format_ARGB32);frame.fill(QColor(205,212,222));
        QPainter painter(&frame);painter.drawImage(20,20,component);painter.end();
        if(!frame.save(outDir+"/"+name+".png"))return false;
        auto m=tb.debugMetrics();m.insert("name",name);metrics.append(m);return true;
    };
    save("toolbar-expanded",FerryToolbar::DockTop,true,false,false,false,false);
    auto before=tb.debugMetrics();
    save("toolbar-quality",FerryToolbar::DockTop,true,false,false,false,true);
    auto after=tb.debugMetrics();
    if(before["barX"]!=after["barX"]||before["barY"]!=after["barY"])return 4;
    save("toolbar-handle-20px",FerryToolbar::DockTop,false,false,false,false,false);
    if(tb.debugMetrics()["barHeight"].toInt()!=qRound(20*tb.debugMetrics()["dpiScale"].toDouble()))return 5;
    save("toolbar-privacy-muted",FerryToolbar::DockTop,true,true,false,true,false);
    save("toolbar-audio-host",FerryToolbar::DockTop,true,false,true,false,false);
    for(auto edge:{FerryToolbar::DockTop,FerryToolbar::DockBottom,FerryToolbar::DockLeft,FerryToolbar::DockRight}){
        save(QString("toolbar-dock-%1").arg((int)edge),edge,true,false,false,false,true);
        auto m=tb.debugMetrics();
        QRect panel(m["panelX"].toInt(),m["panelY"].toInt(),m["panelWidth"].toInt(),m["panelHeight"].toInt());
        QRect bounds(m["boundsX"].toInt(),m["boundsY"].toInt(),m["boundsWidth"].toInt(),m["boundsHeight"].toInt());
        if(!bounds.contains(panel))return 6;
    }
    SDL_SetWindowSize(stream,380,420);
    save("toolbar-quality-narrow",FerryToolbar::DockTop,true,false,false,false,true);
    auto m=tb.debugMetrics();
    QFile early(outDir+"/toolbar-metrics.json");if(early.open(QIODevice::WriteOnly)){early.write(QJsonDocument(metrics).toJson());early.close();}
    if(m["panelWidth"].toInt()>m["boundsWidth"].toInt())return 7;
    for(int edge=0;edge<4;edge++)for(QRect bounds:{QRect(0,0,380,420),QRect(-1920,0,1920,1080),QRect(1300,200,560,400)}){
        QSize bar=edge<2?QSize(344,56):QSize(56,344);
        auto closed=FerryToolbarLayout::place(bounds,bar,QSize(),edge);
        auto open=FerryToolbarLayout::place(bounds,bar,QSize(360,230),edge);
        if(closed.bar!=open.bar||!bounds.contains(open.panel))return 8;
    }
    if(std::abs(FerryToolbarLayout::pressScale(60)-.92f)>.001f||FerryToolbarLayout::pressScale(120)!=1.0f||FerryToolbarLayout::easeOut(1)!=1.0f)return 9;
    QFile json(outDir+"/toolbar-metrics.json");if(json.open(QIODevice::WriteOnly))json.write(QJsonDocument(metrics).toJson());
    std::puts("PASS: hidden SDL toolbar renders; panel does not move bar; four dock edges and negative/narrow bounds; 20px handle; press .92 at 60ms and return at 120ms");
    tb.destroy();SDL_DestroyWindow(stream);TTF_Quit();return 0;
}
