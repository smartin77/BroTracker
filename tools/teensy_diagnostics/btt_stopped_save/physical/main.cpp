// Isolated one-shot fixture staging/readback helper. Never part of production.
// All paths/bytes and expected card CID are compiled in; no arbitrary path API.
#include <Arduino.h>
#include <SD.h>
#include <sd_access.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "fixture_bytes.h"
using namespace BroTracker;
bool ready=false;
FsFile retained;
char line[80];unsigned used=0;
bool Identity() {
    cid_t cid;if(!ready || !SD.sdfs.card()->readCID(&cid))return false;
    const auto* bytes=reinterpret_cast<const uint8_t*>(&cid);char hex[33];const char* digits="0123456789abcdef";
    for(unsigned i=0;i<16;++i){hex[2*i]=digits[bytes[i]>>4];hex[2*i+1]=digits[bytes[i]&15];}hex[32]=0;
    Serial.printf("MEDIA %s %lu %u\n",hex,(unsigned long)SD.sdfs.card()->sectorCount(),unsigned(SD.sdfs.fatType()));
    return !std::strcmp(hex,"6f0303534441424310aa001504018b00") && SD.sdfs.card()->sectorCount()==61076480 && SD.sdfs.fatType()==32;
}
bool LegacyFingerprints() {
    for(const char* path:{"/BroTracker/current.btsession","/BroTracker/current.btsession.candidate",
        "/BroTracker/current.btsession.previous","/BroTracker/tune.btdraft"}) {
        if(!SD.sdfs.exists(path)){Serial.printf("LEGACY %s absent\n",path);continue;}
        if(retained.isOpen() || !retained.open(path,O_RDONLY))return false;
        const unsigned bytes=retained.size();uint32_t crc=0xffffffff;uint8_t block[128];bool ok=true;
        for(unsigned at=0;ok && at<bytes;at+=sizeof(block)) {
            const auto n=std::min<unsigned>(sizeof(block),bytes-at);ok=retained.read(block,n)==int(n) && !retained.getError();
            for(unsigned i=0;ok && i<n;++i){crc^=block[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}
        }
        const bool closed=retained.close();if(!ok || !closed)return false;
        Serial.printf("LEGACY %s %u %08lx\n",path,bytes,(unsigned long)(crc^0xffffffff));
    }
    // Positively identify old registry collisions without opening/deleting them.
    // This lets the host distinguish a known exclusive-create refusal from IO.
    for(unsigned slot=0;slot<2;++slot)for(unsigned generation=1;generation<=16;++generation) {
        char path[96];std::snprintf(path,sizeof(path),"/BroTracker/.btt-load/00000001-%u-%08x.btt",slot,generation);
        if(SD.sdfs.exists(path))Serial.printf("BACKING %u %u\n",slot,generation);
    }
    return true;
}
void Stage() {
    if(!SdAccess::AcquireWrite()){Serial.println("ERROR lease");return;}
    if(!Identity() || retained.isOpen() || SD.sdfs.exists(kDirectory) || !SD.sdfs.mkdir(kDirectory,true)) {
        Serial.println("ERROR identity-or-collision");SdAccess::ReleaseWrite();return;
    }
    bool ok=retained.open(kPath,O_RDWR|O_CREAT|O_EXCL);
    for(unsigned at=0;ok && at<sizeof(kFixture);at+=512) {
        const auto n=std::min<unsigned>(512,sizeof(kFixture)-at);
        ok=retained.write(kFixture+at,n)==n && !retained.getError();
    }
    const bool synced=ok && retained.sync();const bool closed=retained.isOpen() && retained.close();
    Serial.println(synced && closed?"STAGED":"ERROR write-sync-close");SdAccess::ReleaseWrite();
}
void Dump() {
    SdReadScope lease;if(!lease || !Identity() || retained.isOpen() || !retained.open(kPath,O_RDONLY)) {Serial.println("ERROR dump-open");return;}
    char sibling[sizeof(kPath)+10];std::snprintf(sibling,sizeof(sibling),"%s.candidate",kPath);
    const bool candidate=SD.sdfs.exists(sibling);std::snprintf(sibling,sizeof(sibling),"%s.previous",kPath);
    Serial.printf("RECOVERY %u %u\n",unsigned(candidate),unsigned(SD.sdfs.exists(sibling)));
    const unsigned bytes=retained.size();Serial.printf("BYTES %u\n",bytes);
    uint8_t block[128];bool ok=bytes<=65536;
    for(unsigned at=0;ok && at<bytes;at+=sizeof(block)) {
        const auto n=std::min<unsigned>(sizeof(block),bytes-at);ok=retained.read(block,n)==int(n) && !retained.getError();
        if(ok){Serial.printf("DATA %u ",at);for(unsigned i=0;i<n;++i)Serial.printf("%02x",block[i]);Serial.println();}
    }
    const bool closed=retained.close();Serial.println(ok && closed?"DUMPED":"ERROR dump-read-close");
}
void setup(){Serial.begin(115200);ready=SD.begin(BUILTIN_SDCARD);}
void loop(){
    while(Serial.available()) {
        const char c=Serial.read();if(c=='\r')continue;
        if(c=='\n'){
            line[used]=0;used=0;
            if(!std::strcmp(line,"INFO")){SdReadScope lease;if(!lease || !Identity() || !LegacyFingerprints())Serial.println("ERROR identity-or-read");else Serial.println("IDENTIFIED");}
            else if(!std::strcmp(line,kStageCommand))Stage();
            else if(!std::strcmp(line,"DUMP"))Dump();
            else Serial.println("ERROR command");
        }else if(used<sizeof(line)-1)line[used++]=c;
    }
}
