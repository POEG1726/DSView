// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtCore>
#include <zstd.h>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition,const char *message) {
    if(!condition)throw std::runtime_error(message);
}
void require(bool condition,const QString &message) {
    if(!condition)throw std::runtime_error(message.toUtf8().constData());
}
uint64_t number(const char *data,size_t length) {
    require(length>0,"Empty sample index"); uint64_t value=0;
    for(size_t i=0;i<length;i++) {
        const unsigned digit=unsigned(data[i]-'0');
        require(digit<10 && value<=(std::numeric_limits<uint64_t>::max()-digit)/10,"Invalid sample index");
        value=value*10+digit;
    }
    return value;
}
class Merger {
    QFile output_;
    QByteArray buffer_,schema_,utc_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    uint64_t rate_=0,rows_=0,skipped_=0,outputBytes_=0,lastInput_=0,lastState_=0,lastOutput_=0,closedEnd_=0;
    int channels_=0;
    bool haveInput_=false,haveClosed_=false,lastHasEnd_=false;
    QJsonArray sources_;
public:
    explicit Merger(const QString &path):output_(path+".part") {
        require(!QFile::exists(path),"Output already exists");
        require(output_.open(QIODevice::WriteOnly|QIODevice::NewOnly),output_.errorString());
        append("; DSView merged CSV; original absolute sample indices retained\n");
        append("; Source tail may be unfinalized; see the merge report for coverage\n");
    }
    void append(const QByteArray &bytes) { buffer_+=bytes;if(buffer_.size()>=4*1024*1024)flush(); }
    void appendRow(const char *line,int length) {
        buffer_.append(line,length);buffer_+='\n';if(buffer_.size()>=4*1024*1024)flush();
    }
    void flush() {
        qint64 offset=0;
        while(offset<buffer_.size()) {
            const qint64 n=output_.write(buffer_.constData()+offset,buffer_.size()-offset);
            require(n>0,output_.errorString());offset+=n;
        }
        hash_.addData(buffer_);outputBytes_+=uint64_t(buffer_.size());buffer_.clear();
    }
    void merge(const QString &path,bool last) {
        const QFileInfo before(path);QFile input(path);
        require(input.open(QIODevice::ReadOnly),input.errorString());
        QCryptographicHash sourceHash(QCryptographicHash::Sha256);
        const uint64_t rowsBefore=rows_,skippedBefore=skipped_;
        uint64_t start=0,end=0,fileRows=0,fileLast=0;
        bool hasStart=false,hasEnd=false,hasSchema=false,hasRate=false,hasUtc=false;
        auto line=[&](const char *text,int length) {
            if(length && text[length-1]=='\r')--length;
            if(!length)return;
            if(text[0]==';' || (length>=7 && std::memcmp(text,"Sample,",7)==0)) {
                const QByteArray value(text,length);
                if(value.startsWith("; Start sample (inclusive): ")) {
                    require(!hasStart,"Duplicate segment start");start=number(text+28,size_t(length-28));hasStart=true;
                    require((haveClosed_ && start==closedEnd_) || (!haveClosed_ && sources_.isEmpty() && start==0),"Gap or overlap between source segments");
                } else if(value.startsWith("; End sample (exclusive): ")) {
                    require(!hasEnd,"Duplicate segment end");end=number(text+26,size_t(length-26));hasEnd=true;
                } else if(value.startsWith("; Sample rate (Hz): ")) {
                    const uint64_t rate=number(text+20,size_t(length-20));
                    require(rate>0 && (!rate_ || rate_==rate),"Sample rate changed");rate_=rate;hasRate=true;
                } else if(value.startsWith("; Recorder initialized UTC: ")) {
                    const QByteArray utc=value.mid(28);require(utc_.isEmpty() || utc_==utc,"Capture identity changed");utc_=utc;hasUtc=true;
                } else if(value.startsWith("Sample,")) {
                    require(!hasSchema,"Duplicate schema inside a segment");hasSchema=true;
                    if(schema_.isEmpty()) {
                        schema_=value;
                        // Names are CSV quoted, so commas inside quoted names do not add channels.
                        bool quoted=false;int fields=1;
                        for(int i=0;i<value.size();i++) {
                            if(value[i]=='"') { if(quoted && i+1<value.size() && value[i+1]=='"')++i;else quoted=!quoted; }
                            else if(value[i]==',' && !quoted)++fields;
                        }
                        require(!quoted && fields>=3 && fields<=66,"Invalid channel schema");channels_=fields-2;
                        append("; Recorder initialized UTC: "+utc_+'\n');
                        append("; Sample rate (Hz): "+QByteArray::number(rate_)+'\n');
                        append("; Start sample (inclusive): 0\n");append(schema_+'\n');
                    } else require(schema_==value,"Channel names or order changed");
                }
                return;
            }
            require(hasStart && hasSchema && hasRate && hasUtc && !hasEnd,"Data outside a valid segment header/end");
            const char *comma=static_cast<const char *>(std::memchr(text,',',size_t(length)));
            require(comma!=nullptr,"Missing sample delimiter");
            const uint64_t index=number(text,size_t(comma-text));
            const char *states=static_cast<const char *>(std::memchr(comma+1,',',size_t(text+length-comma-1)));
            require(states!=nullptr && states>comma+1,"Missing timestamp or channel fields");++states;
            require(text+length-states==channels_*2-1,"Channel field count differs from schema");
            uint64_t state=0;
            for(int c=0;c<channels_;c++) {
                require(states[c*2]=='0' || states[c*2]=='1',"Invalid channel value");
                require(c==channels_-1 || states[c*2+1]==',',"Invalid channel delimiter");
                state|=uint64_t(states[c*2]-'0')<<c;
            }
            require((fileRows==0 && index==start) || (fileRows>0 && index>fileLast),"Unordered sample within a segment");
            require(!haveInput_ || index>lastInput_,"Unordered sample between segments");
            const bool duplicateInitial=fileRows==0 && haveInput_ && state==lastState_;
            if(duplicateInitial)++skipped_;
            else {appendRow(text,length);++rows_;lastOutput_=index;}
            ++fileRows;fileLast=index;lastInput_=index;lastState_=state;haveInput_=true;
        };
        ZSTD_DCtx *context=ZSTD_createDCtx();require(context!=nullptr,"Cannot allocate decompressor");
        QByteArray carry;QByteArray decoded(1024*1024,Qt::Uninitialized);size_t code=0;
        try {
            while(!input.atEnd()) {
                const QByteArray bytes=input.read(1024*1024);require(!bytes.isEmpty(),input.errorString());sourceHash.addData(bytes);
                ZSTD_inBuffer compressed{bytes.constData(),size_t(bytes.size()),0};
                do {
                    ZSTD_outBuffer plain{decoded.data(),size_t(decoded.size()),0};
                    const size_t previous=compressed.pos;code=ZSTD_decompressStream(context,&plain,&compressed);
                    require(!ZSTD_isError(code),QString("Compressed frame verification failed: %1").arg(ZSTD_getErrorName(code)));
                    const char *pos=decoded.constData(),*limit=pos+plain.pos;
                    while(pos<limit) {
                        const char *newline=static_cast<const char *>(std::memchr(pos,'\n',size_t(limit-pos)));
                        if(!newline) {carry.append(pos,int(limit-pos));break;}
                        if(carry.isEmpty())line(pos,int(newline-pos));
                        else {carry.append(pos,int(newline-pos));line(carry.constData(),carry.size());carry.clear();}
                        pos=newline+1;
                    }
                    require(carry.size()<1024*1024,"Unreasonably long CSV line");
                    require(compressed.pos>previous || plain.pos>0,"Decompressor made no progress");
                    if(compressed.pos==compressed.size && plain.pos<plain.size)break;
                } while(compressed.pos<compressed.size || code!=0);
            }
            require(code==0,"Source ends inside a compressed frame");
            require(carry.isEmpty(),"Source ends inside a CSV row");
        } catch(...) {ZSTD_freeDCtx(context);throw;}
        ZSTD_freeDCtx(context);input.close();
        require(hasStart && hasSchema && hasRate && hasUtc && fileRows>0,"Incomplete source header or empty segment");
        require(!hasEnd || (end>fileLast && end>=start),"Invalid segment end bound");
        require(hasEnd || (last && path.endsWith(".part")),"Missing end marker in a finalized segment");
        const QFileInfo after(path);
        require(before.size()==after.size() && before.lastModified()==after.lastModified(),"Source changed while reading");
        QJsonObject source;
        source["path"]=QDir::toNativeSeparators(before.absoluteFilePath());source["bytes"]=double(before.size());
        source["sha256"]=QString::fromLatin1(sourceHash.result().toHex());
        source["start_sample"]=QString::number(start);source["last_data_row_sample"]=QString::number(fileLast);
        source["input_rows"]=QString::number(fileRows);source["merged_rows"]=QString::number(rows_-rowsBefore);
        source["duplicate_initial_rows_removed"]=QString::number(skipped_-skippedBefore);
        source["compression_checksums_valid"]=true;source["has_end_marker"]=hasEnd;
        source["end_sample_exclusive"]=hasEnd?QJsonValue(QString::number(end)):QJsonValue(QJsonValue::Null);
        sources_.append(source);
        if(hasEnd) {closedEnd_=end;haveClosed_=true;}lastHasEnd_=hasEnd;
        std::printf("%s: %llu rows, start=%llu, last=%llu, end=%s\n",qPrintable(before.fileName()),
            (unsigned long long)fileRows,(unsigned long long)start,(unsigned long long)fileLast,hasEnd?qPrintable(QString::number(end)):"unknown (unfinalized tail)");
        std::fflush(stdout);
    }
    QJsonObject finish() {
        if(lastHasEnd_)append("; End sample (exclusive): "+QByteArray::number(closedEnd_)+'\n');
        else {
            append("; Final end sample is unknown: the last source has no end marker\n");
            append("; Closed source prefix end (exclusive): "+QByteArray::number(closedEnd_)+'\n');
            append("; Last data row sample: "+QByteArray::number(lastOutput_)+'\n');
        }
        flush();require(output_.flush(),output_.errorString());output_.close();
        const QString finalPath=output_.fileName().left(output_.fileName().size()-5);
        require(output_.rename(finalPath),output_.errorString());
        QJsonObject result;
        result["status"]=lastHasEnd_?"merged_complete_segments":"merged_with_unfinalized_source_tail";
        result["output_csv"]=QDir::toNativeSeparators(finalPath);result["output_bytes"]=QString::number(outputBytes_);
        result["output_sha256"]=QString::fromLatin1(hash_.result().toHex());result["sample_rate_hz"]=QString::number(rate_);
        result["columns"]=QString::fromUtf8(schema_);result["output_rows"]=QString::number(rows_);
        result["duplicate_initial_rows_removed"]=QString::number(skipped_);result["last_data_row_sample"]=QString::number(lastOutput_);
        result["closed_source_prefix_end_exclusive"]=QString::number(closedEnd_);
        result["final_end_sample_exclusive"]=lastHasEnd_?QJsonValue(QString::number(closedEnd_)):QJsonValue(QJsonValue::Null);
        result["source_files"]=sources_;return result;
    }
};
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=4) {std::fprintf(stderr,"Usage: merge_loop_csv INPUT_DIRECTORY OUTPUT_CSV REPORT_JSON\n");return 2;}
    try {
        const QDir directory(QString::fromLocal8Bit(argv[1]));
        const auto names=directory.entryList({"capture_*.csv.zst","capture_*.csv.zst.part"},QDir::Files,QDir::Name);
        require(!names.isEmpty(),"No source segments found");
        const QString output=QString::fromLocal8Bit(argv[2]),report=QString::fromLocal8Bit(argv[3]);
        require(QDir().mkpath(QFileInfo(output).absolutePath()),"Cannot create output directory");
        require(!QFile::exists(report),"Report already exists");
        Merger merger(output);
        for(int i=0;i<names.size();i++)merger.merge(directory.filePath(names[i]),i==names.size()-1);
        const QJsonObject result=merger.finish();
        QSaveFile json(report);require(json.open(QIODevice::WriteOnly),json.errorString());
        require(json.write(QJsonDocument(result).toJson(QJsonDocument::Indented))>0,json.errorString());
        require(json.commit(),json.errorString());
        std::printf("Merged %d files into %s\n",names.size(),qPrintable(output));return 0;
    } catch(const std::exception &error) {std::fprintf(stderr,"Merge failed: %s\n",error.what());return 1;}
}
