#include "ui/host_wall_clock.h"
#include "scheduler.h"
#include <stdexcept>
#include <string>

void TestProductionClock() {
    using namespace BroTracker;
    const auto check=[](bool condition) { if(!condition)throw std::runtime_error("production wall-clock regression"); };
    std::uint32_t fields[6]{};
    for(const char* bad:{"BTCLOCK1 SET 1 1 2 4294967296 2 840",
                        "BTCLOCK1 SET 1 1 2 -1700000000 2 840",
                        "BTCLOCK1 SET 1 1 2 1700000000 2 840 extra",
                        "BTCLOCK1 SET 2 1 2 1700000000 2 840"})
        check(!ParseClockFields(bad,"BTCLOCK1 SET ",fields,6));
    check(ParseClockFields("BTCLOCK1 SET 1 1 2 1700000000 2 840","BTCLOCK1 SET ",fields,6));
    WallClockDevice device;
    Scheduler scheduler;
    scheduler.Initialize();
    scheduler.AdvanceSamples(128);
    using Reply=WallClockDevice::Result;
    check(!device.Query(1,0));
    device.NewConnection();
    const auto connection=device.Generation();
    check(device.Quality(1546300800)==ClockQuality::Unknown);
    check(device.Quality(1700000000)==ClockQuality::Retained);
    check(device.Query(1,0));
    check(device.Set(1,connection,4102444800u,2,840,1)==Reply::Invalid);
    check(device.Set(1,connection,1700000000,2,840,2)==Reply::Stale);
    check(device.Query(2,10));
    check(device.Set(2,connection,1700000001,3,900,11)==Reply::Applied);
    check(device.Offset()==60);
    check(FilesystemCalendarSeconds(1700000001,device.Offset())==1700003601);
    check(device.Query(3,12));
    check(device.Set(3,connection,1700000000,3,960,13)==Reply::Applied);
    check(device.Offset()==120);
    check(device.Query(4,14));
    check(device.Set(4,connection,1700001000,2,780,15)==Reply::Kept);
    check(device.Quality(1700000000)==ClockQuality::Synchronized);
    check(device.Offset()==-60);
    check(FilesystemCalendarSeconds(1700000000,-60)==1699996400);
    check(device.Query(5,16));
    check(device.Set(5,connection,1700002000,3,840,5016)==Reply::Stale);
    device.NewConnection();
    check(device.Query(6,5017));
    check(device.Set(6,connection,1700003000,3,840,5018)==Reply::Stale);
    check(device.Set(6,device.Generation(),1700003000,3,840,5019)==Reply::Applied);
    check(scheduler.GetPosition()==128);
    scheduler.AdvanceSamples(128);
    check(scheduler.GetPosition()==256);

    HostWallClock host;
    OsClockSample os{1700000000000LL,60,ClockQuality::Host,true};
    const auto read=[&]{return os;};
    char wire[96];
    const auto command=[&](unsigned now){
        const auto size=host.Command(wire,sizeof(wire),now,read);
        return std::string(wire,size);
    };
    check(command(0).empty());
    host.Advertise();
    check(command(0)=="BTCLOCK1 GET 1 1\n");
    check(!host.Response("BTCLOCK1 STATE 1 2 1 0 0"));
    check(host.Response("BTCLOCK1 STATE 1 1 1 0 0"));
    os.milliseconds+=1000;
    check(command(10)=="BTCLOCK1 SET 1 1 1 1700000001 2 900\n");
    check(!host.Response("BTCLOCK1 ACK 1 1 2 1700000001 2"));
    check(host.Response("BTCLOCK1 ACK 1 1 1 1700000001 2"));
    os.milliseconds+=2000;
    check(command(2010).empty());
    os.milliseconds-=5000;
    check(command(4010)=="BTCLOCK1 GET 1 2\n");
    check(host.Response("BTCLOCK1 STATE 1 2 1 1700000005 2"));
    check(command(4020).find("1699999998")!=std::string::npos);
    check(host.Response("BTCLOCK1 ACK 1 2 1 1699999998 2"));
    os.milliseconds+=2000;os.offset=120;
    check(command(6020)=="BTCLOCK1 GET 1 3\n");
    host.Disconnect();host.Advertise();
    check(!host.Response("BTCLOCK1 STATE 1 3 1 0 0"));
    check(command(6030)=="BTCLOCK1 GET 1 4\n");
}
