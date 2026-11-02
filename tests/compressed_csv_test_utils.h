// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QFile>
#include <QString>
#include <zstd.h>

// Decode every frame and verify frame checksums, including concatenated frames.
inline QByteArray readCompressedCsv(const QString &path, bool *complete=nullptr) {
    if(complete)*complete=false;
    QFile file(path); if(!file.open(QIODevice::ReadOnly))return {};
    const QByteArray input=file.readAll();
    ZSTD_DCtx *context=ZSTD_createDCtx(); if(!context)return {};
    ZSTD_inBuffer source{input.constData(),size_t(input.size()),0};
    QByteArray result; size_t code=1; char buffer[65536];
    while(source.pos<source.size || code!=0) {
        ZSTD_outBuffer target{buffer,sizeof(buffer),0};
        const size_t before=source.pos;
        code=ZSTD_decompressStream(context,&target,&source);
        if(ZSTD_isError(code)) { ZSTD_freeDCtx(context); return {}; }
        if(before==source.pos && target.pos==0) break;
        result.append(buffer,int(target.pos));
    }
    ZSTD_freeDCtx(context);
    if(complete)*complete=(code==0);
    return result;
}
