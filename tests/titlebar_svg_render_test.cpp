// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QFile>
#include <cstdio>
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    int failures=0;
    for (const char *theme : {"dark", "light"}) {
        for (const char *name : {"minimize", "maximize", "restore", "close"}) {
            QString path=QString(":/icons/%1/%2.svg").arg(theme,name);
            QIcon icon(path);
            QImage img=icon.pixmap(32,32).toImage();
            bool visible=false;
            for(int y=0;y<img.height() && !visible;++y) for(int x=0;x<img.width();++x) if(qAlpha(img.pixel(x,y))) {visible=true;break;}
            bool ok=QFile::exists(path) && !img.isNull() && visible;
            std::printf("%s: %s\n",qPrintable(path),ok?"ok":"FAILED");
            if(!ok) ++failures;
        }
    }
    return failures ? 1 : 0;
}
