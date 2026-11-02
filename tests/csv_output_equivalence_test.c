// SPDX-License-Identifier: GPL-3.0-or-later
#include "../libsigrok4DSL/libsigrok-internal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../common/log/xlog.h"
xlog_writer *sr_log;
int xlog_err(xlog_writer *wr, const char *format, ...) { (void)wr; (void)format; return 0; }
extern struct sr_output_module output_csv;
char *sr_samplerate_string(uint64_t x) { (void)x; return g_strdup("50 MHz"); }
char *sr_samplecount_string(uint64_t x) { (void)x; return g_strdup("test"); }
static void feed_meta(struct sr_output *o) {
    struct sr_config rate = { SR_CONF_SAMPLERATE, g_variant_new_uint64(50000000) };
    struct sr_config depth = { SR_CONF_LIMIT_SAMPLES, g_variant_new_uint64(256) };
    struct sr_datafeed_meta meta = {0};
    struct sr_datafeed_packet p = {0}; GString *out = NULL;
    meta.config = g_slist_append(meta.config, &rate); meta.config = g_slist_append(meta.config, &depth);
    p.type = SR_DF_META; p.payload = &meta; output_csv.receive(o, &p, &out); g_slist_free(meta.config);
}
static int test(uint64_t start, unsigned int seed) {
    struct sr_dev_inst dev = {0}; struct sr_channel ch[2] = {{0}};
    struct sr_output a = {0}, b = {0}; GHashTable *opts = g_hash_table_new(g_str_hash,g_str_equal);
    GVariant *type = g_variant_ref_sink(g_variant_new_int16(SR_CHANNEL_LOGIC));
    g_hash_table_insert(opts, "type", type);
    for(int i=0;i<2;i++) { ch[i].index=i; ch[i].enabled=1; ch[i].type=SR_CHANNEL_LOGIC; ch[i].name=i?"B":"A"; dev.channels=g_slist_append(dev.channels,&ch[i]); }
    a.sdi=&dev; b.sdi=&dev; a.start_sample_index=start; b.start_sample_index=start;
    strcpy(a.time_string,"test"); strcpy(b.time_string,"test");
    if(output_csv.init(&a,opts)!=SR_OK || output_csv.init(&b,opts)!=SR_OK) return 1;
    feed_meta(&a); feed_meta(&b);
    uint8_t samples[256], values[256]; uint64_t indexes[256]; unsigned int n=0; uint8_t prev=0;
    for(unsigned int i=0;i<256;i++) { seed=seed*1664525u+1013904223u; samples[i]=((seed >> 27)&3); if ((seed & 3)!=0 && i) samples[i]=samples[i-1]; if(i==0 || samples[i]!=prev) { indexes[n]=start+i; values[n++]=samples[i]; prev=samples[i]; } }
    struct sr_datafeed_logic logic={0}; struct sr_datafeed_packet packet={0};
    GString *old=g_string_new(NULL), *sparse=g_string_new(NULL), *chunk=NULL;
    unsigned int edge=0;
    logic.unitsize=1; packet.type=SR_DF_LOGIC; packet.payload=&logic;
    for(unsigned int first=0;first<256;first+=32) {
        logic.data=samples+first; logic.length=32;
        output_csv.receive(&a,&packet,&chunk); g_string_append_len(old,chunk->str,chunk->len); g_string_free(chunk,TRUE);
        unsigned int begin=edge;
        while(edge<n && indexes[edge] < start+first+32) ++edge;
        if(edge>begin) {
            logic.data=values+begin; logic.length=edge-begin;
            sr_csv_receive_sparse(&b,&logic,indexes+begin,&chunk);
            g_string_append_len(sparse,chunk->str,chunk->len); g_string_free(chunk,TRUE);
        }
    }
    int bad=old->len!=sparse->len || memcmp(old->str,sparse->str,old->len)!=0;
    if(bad) fprintf(stderr,"CSV mismatch at start=%llu seed=%u\n",(unsigned long long)start,seed);
    if(old)g_string_free(old,TRUE); if(sparse)g_string_free(sparse,TRUE);
    output_csv.cleanup(&a); output_csv.cleanup(&b); g_slist_free(dev.channels); g_hash_table_destroy(opts); g_variant_unref(type);
    return bad;
}
int main(void) {
    for(unsigned int i=0;i<1000;i++) if(test((i%3)*8192, i+1)) return 1;
    puts("1000 actual CSV formatter comparisons passed (including nonzero start indexes)"); return 0;
}
