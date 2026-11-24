#include "waveform_disk_cache.h"
#include "bringup/session_lineage.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace BroTracker {
namespace {
namespace fs = std::filesystem;
constexpr unsigned kQueueLimit = 1024;
constexpr unsigned kRecordHeader = 12;
using Bytes = std::vector<std::uint8_t>;

void Put(Bytes& out, std::uint64_t value, unsigned count = 4) {
    for(unsigned i=0;i<count;++i)out.push_back(value>>(8*i));
}
std::uint32_t Get(const std::uint8_t* data) {
    std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=std::uint32_t(data[i])<<(8*i);
    return value;
}
std::uint32_t Checksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc=~0u;
    while(size--) {
        crc^=*data++;
        for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
    }
    return ~crc;
}
Bytes Header(std::string_view path, WaveformSource source, WaveformFingerprint fp) {
    Bytes out{'B','T','W','C',1};
    Put(out,1); // Prepared fingerprint version.
    Put(out,source.frames);Put(out,source.rate);Put(out,source.channels);Put(out,source.bits);
    Put(out,fp.file_bytes,8);Put(out,fp.prefix_digest,8);Put(out,fp.pcm_offset);Put(out,fp.prefix_bytes);
    Put(out,path.size());
    out.insert(out.end(),path.begin(),path.end());
    Put(out,Checksum(out.data(),out.size()));
    return out;
}
bool OwnedFile(const fs::directory_entry& entry) {
    if(entry.symlink_status().type()!=fs::file_type::regular)return false;
    const auto name=entry.path().filename().string();
    if(name.size()!=53 || name.substr(49)!=".bwc" || name[32]!='-' ||
       !ValidSessionLineage(std::string_view(name).substr(0,32)))return false;
    return name.find_first_not_of("0123456789abcdef",33)==49;
}
class CacheLock {
public:
    explicit CacheLock(const fs::path& path) {
#ifdef _WIN32
        handle_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
#else
        fd_=open(path.c_str(),O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd_>=0 && flock(fd_,LOCK_EX|LOCK_NB)!=0) {close(fd_);fd_=-1;}
#endif
    }
    ~CacheLock() {
#ifdef _WIN32
        if(Valid())CloseHandle(handle_);
#else
        if(Valid())close(fd_);
#endif
    }
    bool Valid() const {
#ifdef _WIN32
        return handle_!=INVALID_HANDLE_VALUE;
#else
        return fd_>=0;
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_;
#else
    int fd_;
#endif
};

bool Commit(FILE* file) {
    if(std::fflush(file))return false;
#ifdef _WIN32
    return _commit(_fileno(file))==0;
#else
    return fsync(fileno(file))==0;
#endif
}
FILE* OpenAppend(const fs::path& path) {
#ifdef _WIN32
    return _wfopen(path.c_str(),L"ab");
#else
    return std::fopen(path.c_str(),"ab");
#endif
}
}

struct WaveformDiskCache::Worker {
    struct Job {
        enum class Kind { Lineage, Lookup, Record };
        Kind kind;
        std::uint64_t ticket=0;
        std::string lineage,path;
        WaveformSource source{};
        WaveformFingerprint fp{};
        std::uint32_t offset=0;
        Bytes pcm;
    };
    Worker(fs::path directory,std::uint64_t limit)
        :root(std::move(directory)),budget(limit),thread(&Worker::Run,this) {}
    ~Worker() {
        { std::lock_guard<std::mutex> lock(mutex); closing=true; }
        wake.notify_one();
        thread.join();
    }
    bool Submit(Job job) {
        std::lock_guard<std::mutex> lock(mutex);
        if(queue.size()>=kQueueLimit)return false;
        queue.push_back(std::move(job));
        wake.notify_one();
        return true;
    }
    Result Poll() {
        std::lock_guard<std::mutex> lock(mutex);
        Result ready=std::move(result);
        result={};
        return ready;
    }
    void Publish(Result value) {
        Result previous;
        { std::lock_guard<std::mutex> lock(mutex); previous=std::move(result); result=std::move(value); }
    }
    void Run() {
        try {
            fs::create_directories(root);
            if(fs::symlink_status(root).type()!=fs::file_type::directory)throw std::runtime_error("cache directory");
            CacheLock lock(root/"lock");
            if(!lock.Valid())throw std::runtime_error("cache locked");
            if(budget)Reserve(0);
            RunJobs();
        } catch(const std::exception&) {
            RunJobs(false);
        }
    }
    void RunJobs(bool enabled=true) {
        std::deque<Job> batch;
        for(;;) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock,[&]{return closing || !queue.empty();});
                if(queue.empty())return;
                if(!closing && queue.front().kind==Job::Kind::Record)
                    wake.wait_for(lock,std::chrono::milliseconds(120),[&]{
                        return closing || queue.size()>=128 || queue.back().kind!=Job::Kind::Record;
                    });
                batch.swap(queue);
            }
            for(auto it=batch.begin();it!=batch.end();) {
                auto end=std::next(it);
                try {
                    if(it->kind==Job::Kind::Lookup) {
                        const auto id=it->ticket;
                        auto cache=enabled?Load(*it):nullptr;
                        Publish({id,std::move(cache)});
                    } else if(it->kind==Job::Kind::Lineage) {
                        if(enabled)Activate(it->lineage);
                    } else {
                        while(end!=batch.end() && end->kind==Job::Kind::Record && end->ticket==it->ticket)++end;
                        if(enabled && ticket==it->ticket && writable)Write(it,end);
                    }
                } catch(const std::exception&) {
                    writable=false;
                    if(it->kind==Job::Kind::Lookup)Publish({it->ticket,{}});
                }
                it=end;
            }
            batch.clear();
        }
    }
    void Activate(const std::string& lineage) {
        if(budget || lineage==active_lineage)return;
        writable=false;
        for(const auto& entry:fs::directory_iterator(root))
            if(OwnedFile(entry) && entry.path().filename().string().compare(0,32,lineage)!=0)
                fs::remove(entry);
        active_lineage=lineage;
    }
    void Touch() {
        // Advance relative to the stored ordering even when OS wall time moves backward.
        const auto step=std::chrono::seconds(2);
        const auto old=fs::last_write_time(file);
        newest=std::max(newest,old);
        if(newest<fs::file_time_type::max()-step) {
            newest+=step;
            fs::last_write_time(file,newest);
        }
    }
    std::unique_ptr<SampleWaveformCache> Load(const Job& job) {
        ticket=job.ticket;
        writable=false;
        if(!budget && active_lineage!=job.lineage)return {};
        header=Header(job.path,job.source,job.fp);
        char name[22];
        std::snprintf(name,sizeof(name),"-%016llx.bwc",
                     static_cast<unsigned long long>(WaveformPrefixDigest(header.data(),header.size())));
        file=root/(job.lineage+name);
        const auto type=fs::symlink_status(file).type();
        committed=0;
        if(type==fs::file_type::not_found) {writable=true;return {};}
        if(type!=fs::file_type::regular)return {};
        std::ifstream in(file,std::ios::binary);
        Bytes actual(header.size());
        if(!in.read(reinterpret_cast<char*>(actual.data()),actual.size()) || actual!=header) {
            // A torn newly-created header has no committed PCM; a different complete
            // header may be a key collision and must never be adopted or overwritten.
            if(fs::file_size(file)<header.size()) {in.close();fs::remove(file);writable=true;}
            return {};
        }
        auto cache=std::make_unique<SampleWaveformCache>();
        if(!cache->Begin(job.source))return {};
        committed=header.size();
        std::array<std::uint8_t,kRecordHeader+kWaveformIdleBytes> record{};
        while(in.read(reinterpret_cast<char*>(record.data()),kRecordHeader)) {
            const auto offset=Get(record.data()), size=Get(record.data()+4), checksum=Get(record.data()+8);
            const auto stride=job.source.Stride();
            if(!size || size>kWaveformIdleBytes || offset%stride || size%stride ||
               std::uint64_t(offset)+size>job.source.Bytes() ||
               !in.read(reinterpret_cast<char*>(record.data()+kRecordHeader),size))break;
            std::memmove(record.data()+8,record.data()+kRecordHeader,size);
            if(Checksum(record.data(),8+size)!=checksum)break;
            if(cache->Status()!=SampleWaveformCache::State::Ready &&
               !cache->StorageLimited() && !cache->Append(offset,record.data()+8,size)) {
                if(!cache->StorageLimited())break;
            }
            committed+=kRecordHeader+size;
        }
        const bool read_error=in.bad();
        in.close();
        if(read_error)return {};
        writable=true;
        observed_size=fs::file_size(file);
        if(budget) {
            for(const auto& entry:fs::directory_iterator(root))
                if(OwnedFile(entry))newest=std::max(newest,entry.last_write_time());
            Touch();
        }
        return cache->BytesLoaded()?std::move(cache):nullptr;
    }
    bool Reserve(std::uint64_t needed) {
        if(!budget)return true;
        if(needed>budget || committed>budget-needed)return false;
        for(;;) {
            fs::path oldest;
            auto age=fs::file_time_type::max();
            std::uint64_t total=0;
            for(const auto& entry:fs::directory_iterator(root)) {
                if(!OwnedFile(entry))continue;
                const auto size=entry.file_size();
                total+=std::min<std::uint64_t>(size,UINT64_MAX-total);
                const auto used=entry.last_write_time();
                newest=std::max(newest,used);
                if(entry.path()!=file && (oldest.empty() || used<age || (used==age && entry.path()<oldest))) {
                    age=used;oldest=entry.path();
                }
            }
            if(total<=budget-needed)return true;
            if(oldest.empty() || !fs::remove(oldest))return false;
        }
    }
    void Write(std::deque<Job>::iterator first,std::deque<Job>::iterator end) {
        if(committed && observed_size!=committed) {
            fs::resize_file(file,committed);
            observed_size=committed;
        }
        Bytes batch;
        if(!committed)batch=header;
        for(auto it=first;it!=end;++it) {
            Bytes body;
            Put(body,it->offset);Put(body,it->pcm.size());
            body.insert(body.end(),it->pcm.begin(),it->pcm.end());
            batch.insert(batch.end(),body.begin(),body.begin()+8);
            Put(batch,Checksum(body.data(),body.size()));
            batch.insert(batch.end(),it->pcm.begin(),it->pcm.end());
        }
        if(!Reserve(batch.size()))return;
        std::unique_ptr<FILE,decltype(&std::fclose)> output(OpenAppend(file),&std::fclose);
        if(!output || std::fwrite(batch.data(),1,batch.size(),output.get())!=batch.size() ||
           !Commit(output.get())) {writable=false;return;}
        output.reset();
        committed+=batch.size();
        observed_size=committed;
        if(budget)Touch();
    }

    fs::path root,file;
    std::uint64_t budget,ticket=0,committed=0,observed_size=0;
    fs::file_time_type newest=fs::file_time_type::min();
    Bytes header;
    std::string active_lineage;
    bool writable=false,closing=false;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> queue;
    Result result;
    std::thread thread;
};

WaveformDiskCache::WaveformDiskCache(fs::path root,std::uint64_t budget) {
    if(root.is_absolute())worker_=std::make_unique<Worker>(root/(budget?"shared":"session"),budget);
}
WaveformDiskCache::~WaveformDiskCache()=default;

fs::path WaveformDiskCache::UserCachePath() {
#ifdef _WIN32
    const auto* local=_wgetenv(L"LOCALAPPDATA");
    const fs::path base=local?local:L"";
#else
    const auto* xdg=std::getenv("XDG_CACHE_HOME");
    const auto* home=std::getenv("HOME");
    auto base=xdg?fs::path(xdg):fs::path{};
    if(!base.is_absolute() && home && fs::path(home).is_absolute())base=fs::path(home)/".cache";
#endif
    return base.is_absolute()?base/"BroTracker"/"waveforms-v1":fs::path{};
}
bool WaveformDiskCache::SetLineage(std::string_view lineage) {
    if(!worker_ || !ValidSessionLineage(lineage))return false;
    Worker::Job job{Worker::Job::Kind::Lineage};
    job.lineage=lineage;
    return worker_->Submit(std::move(job));
}
bool WaveformDiskCache::Lookup(std::uint64_t ticket,std::string_view lineage,std::string_view path,
                              WaveformSource source,WaveformFingerprint fingerprint) {
    if(!worker_ || !ValidSessionLineage(lineage) || !ValidSamplePath(path))return false;
    Worker::Job job{Worker::Job::Kind::Lookup};
    job.ticket=ticket;job.lineage=lineage;job.path=path;job.source=source;job.fp=fingerprint;
    return worker_->Submit(std::move(job));
}
bool WaveformDiskCache::Save(std::uint64_t ticket,std::uint32_t offset,const std::uint8_t* pcm,unsigned size) {
    if(!worker_ || !size || size>kWaveformIdleBytes)return false;
    Worker::Job job{Worker::Job::Kind::Record};
    job.ticket=ticket;job.offset=offset;job.pcm.assign(pcm,pcm+size);
    return worker_->Submit(std::move(job));
}
WaveformDiskCache::Result WaveformDiskCache::Poll() {
    return worker_?worker_->Poll():Result{};
}
}
