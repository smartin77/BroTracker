// Native-only fresh physical fixture encoder and real schema validation.
#include "tests/btt_pattern_layout.h"
#include "tools/btt/foundation.h"
#include <fstream>
#include <memory>
namespace f=btt_layout;
using Bytes=std::vector<uint8_t>;
struct Reader final:btt::Reader {
    Bytes bytes;explicit Reader(Bytes b):bytes(std::move(b)){}
    uint32_t size()const override{return bytes.size();}
    const void* identity()const override{return this;}
    unsigned read(uint32_t at,void* p,unsigned n)const override{
        if(n>512 || at>bytes.size() || n>bytes.size()-at)return 0;std::memcpy(p,bytes.data()+at,n);return n;
    }
};
struct Writer final:btt::Writer {
    Bytes bytes;
    const void* identity()const override{return this;}
    bool resize(uint32_t n)override{bytes.resize(n);return true;}
    unsigned write(uint32_t at,const void* p,unsigned n)override{
        if(at+n>bytes.size())bytes.resize(at+n);std::memcpy(bytes.data()+at,p,n);return n;
    }
};
Bytes Encode() {
    Bytes tune,ids,order;
    f::Tlv(tune,1,4,{0x9e,0x59,0xe7,0x65,0x26,0x11,0x28,0x51,8,7,6,5,4,3,2,1});
    f::Tlv(tune,2,5,{'S','t','o','p','p','e','d',' ','S','a','v','e'});
    f::Scalar(tune,3,4,13750);f::Scalar(tune,4,1,8);f::Scalar(tune,5,4,1000);f::Scalar(tune,6,4,1000);
    f::Tlv(tune,777,99,{0,255,42},false);
    for(auto id:{17,503,17})f::Put(ids,id,4);f::Tlv(order,1,6,ids);
    std::vector<f::Section> sections{{"TUNE",0,tune},{"ORDR",0,order}};
    for(auto id:{701,29}){Bytes inst;f::Scalar(inst,1,2,3);sections.push_back({"INST",unsigned(id),inst});}
    // Unused Sample has no resource or tuning. Save inserts both defaults;
    // its absence from runtime bindings must not remove it from the document.
    Bytes inst;f::Scalar(inst,1,2,1);f::Tlv(inst,777,99,{0,254},false);sections.push_back({"INST",900,inst});
    // Unsupported explicit values must survive too (unused Sample).
    inst.clear();f::Scalar(inst,1,2,1);f::Scalar(inst,3,2,0,false);f::Scalar(inst,4,2,199,false);
    sections.push_back({"INST",901,inst});
    for(unsigned id:{17,503}) {
        f::Pattern p;p.Clear(id==503?8:2);
        for(unsigned row=0;row<p.rows;++row) {
            f::Cell cell;cell.note=uint8_t(60+row);cell.instrument=row%2?701:29;
            if(id==17){cell.fx={601,602,603};cell.values={0,42,255};}
            p.Put(row*8+(row%2),cell);
        }
        Bytes payload;if(!f::Encode(p,3,payload))return {};sections.push_back({"PATT",id,payload});
    }
    sections.push_back({"XTRA",77,Bytes(6000,173),false});return f::Envelope(sections);
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    auto input=std::make_shared<Reader>(Encode());btt::Document document;btt::Workspace work;Writer output;
    if(input->bytes.empty() || btt::Validate(input,document,work)!=btt::Result::Ok ||
       btt::Rewrite(document,output,work)!=btt::Result::Ok)return 2;
    const std::string folder=argv[1];
    for(auto pair:{std::make_pair("input.btt",&input->bytes),std::make_pair("expected.btt",&output.bytes)}) {
        std::ofstream file(folder+"/"+pair.first,std::ios::binary);file.write(reinterpret_cast<const char*>(pair.second->data()),pair.second->size());
        file.close();if(!file)return 3;
    }
    return 0;
}
