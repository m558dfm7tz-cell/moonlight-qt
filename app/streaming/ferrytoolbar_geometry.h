#pragma once
#include <QRect>
#include <QSize>
#include <QtGlobal>
#include <cmath>

namespace FerryToolbarLayout {
struct Placement { QRect bar, panel, canvas; };
inline float easeOut(float value) { float t=qBound(0.0f,value,1.0f); return 1.0f-std::pow(1.0f-t,3.0f); }
inline float pressScale(unsigned elapsed) { return elapsed>=120 ? 1.0f : 1.0f-0.08f*std::sin(3.14159265f*elapsed/120.0f); }
inline Placement place(QRect bounds,QSize bar,QSize panel,int edge,int gap=6,QRect panelBounds=QRect()) {
    if(panelBounds.isEmpty())panelBounds=bounds;
    Placement result;
    int x=bounds.x()+(bounds.width()-bar.width())/2;
    int y=bounds.y()+2;
    if(edge==1)y=bounds.bottom()-bar.height()-1;
    if(edge==2||edge==3){x=edge==2?bounds.x()+2:bounds.right()-bar.width()-1;y=bounds.y()+(bounds.height()-bar.height())/2;}
    x=qBound(panelBounds.left(),x,qMax(panelBounds.left(),panelBounds.right()-bar.width()+1));
    y=qBound(panelBounds.top(),y,qMax(panelBounds.top(),panelBounds.bottom()-bar.height()+1));
    result.bar=QRect(QPoint(x,y),bar);
    result.canvas=result.bar;
    if(!panel.isEmpty()) {
        panel=panel.boundedTo(panelBounds.size());
        int px=x+(bar.width()-panel.width())/2,py=y+bar.height()+gap;
        if(edge==1)py=y-panel.height()-gap;
        if(edge==2){px=x+bar.width()+gap;py=y;}
        if(edge==3){px=x-panel.width()-gap;py=y;}
        px=qBound(panelBounds.left(),px,panelBounds.right()-panel.width()+1);
        py=qBound(panelBounds.top(),py,panelBounds.bottom()-panel.height()+1);
        result.panel=QRect(QPoint(px,py),panel);
        result.canvas=result.canvas.united(result.panel);
    }
    return result;
}
}
