// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject> // Qt defines emit as a macro; keep this regression test representative.
#include "../DSView/pv/data/logiccsvedges.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <utility>
#include <vector>

using Edge = std::pair<uint64_t,uint64_t>;
static std::vector<Edge> reference(const std::vector<uint8_t*> &p, const std::vector<bool> &level, size_t n) {
    std::vector<Edge> out; uint64_t last=0;
    for(size_t i=0;i<n;i++) { uint64_t value=0;
        for(size_t c=0;c<p.size();c++) if(p[c] ? ((p[c][i/8] >> (i%8)) & 1) : level[c]) value|=uint64_t(1)<<c;
        if(i==0 || value!=last) { out.emplace_back(i,value); last=value; }
    } return out;
}
int main() {
    std::mt19937_64 rng(12345);
    for(int test=0;test<1000;test++) {
        size_t channels=1+rng()%16, n=8*(1+rng()%400);
        std::vector<std::vector<uint8_t>> buffers(channels, std::vector<uint8_t>(n/8));
        std::vector<uint8_t*> ptrs; std::vector<bool> levels;
        for(auto &b: buffers) { for(auto &v:b) v=uint8_t(rng()); bool constant=(rng()%3==0); ptrs.push_back(constant?nullptr:b.data()); levels.push_back(rng()%2); }
        auto expected=reference(ptrs,levels,n); std::vector<Edge> got; pv::data::CsvEdgeScanner scanner;
        for(size_t start=0;start<n;) { size_t count=std::min<size_t>(1+rng()%111,n-start); scanner.scan(ptrs,levels,start,count,[&](uint64_t i,uint64_t v){got.emplace_back(i,v);}); start+=count; }
        if(got!=expected) { std::fprintf(stderr,"mismatch test %d\n",test); return 1; }
    }
    // Change from a constant block to another at the same logical boundary.
    {
        pv::data::CsvEdgeScanner scanner; std::vector<Edge> got;
        std::vector<uint8_t*> ptrs(2, nullptr); std::vector<bool> levels(2, false);
        scanner.scan(ptrs, levels, 0, 8192, [&](uint64_t i, uint64_t v){got.emplace_back(i,v);});
        levels[1] = true;
        scanner.scan(ptrs, levels, 0, 8192, [&](uint64_t i, uint64_t v){got.emplace_back(8192+i,v);});
        std::vector<Edge> expected{{0,0},{8192,2}};
        if (got != expected) { std::fputs("block boundary mismatch\n", stderr); return 1; }
    }
    // Include transitions exactly at 64-bit, 8192-sample and block boundaries.
    size_t n=128*1024*1024; size_t nc=6;
    std::vector<std::vector<uint8_t>> data(nc,std::vector<uint8_t>(n/8));
    for(size_t c=0;c<nc;c++) for(size_t i=64+c*17;i<n;i+=65537+c*2) data[c][i/8]^=uint8_t(1u<<(i%8));
    std::vector<uint8_t*> ptrs; std::vector<bool> levels(nc,false); for(auto &v:data)ptrs.push_back(v.data());
    auto t0=std::chrono::steady_clock::now(); auto expected=reference(ptrs,levels,n); auto t1=std::chrono::steady_clock::now();
    std::vector<Edge> got; pv::data::CsvEdgeScanner scanner;
    for(size_t i=0;i<n;i+=524288) scanner.scan(ptrs,levels,i,std::min<size_t>(524288,n-i),[&](uint64_t x,uint64_t v){got.emplace_back(x,v);});
    auto t2=std::chrono::steady_clock::now(); if(got!=expected){std::fputs("benchmark mismatch\n",stderr);return 1;}
    double a=std::chrono::duration<double>(t1-t0).count(), b=std::chrono::duration<double>(t2-t1).count();
    std::printf("1000 randomized cases passed; %zu samples, %zu channels, %zu events: reference %.3f s, sparse %.3f s, %.1fx\n",n,nc,got.size(),a,b,a/b);
}
