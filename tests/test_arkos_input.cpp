#include "test_framework.h"
#include "ui/arkos/input.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
using namespace BroTracker;
namespace {
std::optional<LivePlaybackView> PatternView() {
    LivePlaybackView view; view.telemetry_available=true; view.position.rows=32; view.position.channels=2;
    DevicePatternSnapshot snapshot; snapshot.version=2; snapshot.transfer_id=1; snapshot.tempo_hundredths=12753;
    snapshot.pattern.active_rows=32; snapshot.pattern.active_channels=2;
    snapshot.pattern.cells[0][0]={60,0}; snapshot.pattern.cells[0][1]={62,1}; view.device_pattern=snapshot; return view;
}
struct InputFixture {
    LocalPatternEditor editor;
    ArkOSEditorInput input;
    std::uint32_t now=0;
    explicit InputFixture(ValueStepDirection direction=ValueStepDirection::Normal):input(direction) {
        editor.Sync(PatternView()); input.Select(7);
    }
    ArkOSInputResult Edge(unsigned button,bool down=true,SDL_JoystickID id=7) {
        SDL_Event event{}; event.type=down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
        event.cbutton.which=id; event.cbutton.button=static_cast<Uint8>(button);
        return input.Handle(event,now,true,editor);
    }
    ArkOSInputResult Tap(unsigned button) { const auto result=Edge(button); Edge(button,false); return result; }
};
struct Transport : SerialTransport {
    std::string rx,tx;
    bool Open(FILE*,const char*) override {return true;}
    void Close() override {}
    bool Healthy() override {return true;}
    int Read(char* out,unsigned n) override {n=static_cast<unsigned>(std::min<std::size_t>(n,rx.size())); std::memcpy(out,rx.data(),n); rx.erase(0,n); return static_cast<int>(n);}
    int Write(const char* data,unsigned n) override {tx.append(data,n); return static_cast<int>(n);}
    const char* Error() const override {return "test";}
};
}
std::unique_ptr<SerialTransport> MakeSerialTransport() { return std::make_unique<Transport>(); }
TEST_CASE(ArkOSPlainNavigationAndChordConsumption) {
    InputFixture f;
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN); CHECK_EQ(f.editor.Cursor().row,1);
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_UP); CHECK_EQ(f.editor.Cursor().row,0);
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); CHECK(f.editor.Cursor().field==EditField::Instrument);
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); CHECK_EQ(f.editor.Cursor().channel,1); CHECK(f.editor.Cursor().field==EditField::Note);
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_LEFT); CHECK_EQ(f.editor.Cursor().channel,0); CHECK(f.editor.Cursor().field==EditField::Instrument);
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_LEFT); CHECK(f.editor.Cursor().field==EditField::Note);
    const auto before=*f.editor.Draft(); f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    CHECK(SamePattern(before,*f.editor.Draft()));
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); CHECK_EQ(f.editor.Cursor().channel,1); CHECK(f.editor.Cursor().field==EditField::Note);
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_LEFT); CHECK_EQ(f.editor.Cursor().channel,0);
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_UP); CHECK_EQ(f.editor.Cursor().row,0); CHECK_EQ(f.editor.Draft()->cells[0][0].note,61);
    f.Tap(SDL_CONTROLLER_BUTTON_Y); CHECK(!f.editor.Dirty()); CHECK_EQ(f.editor.Draft()->cells[0][0].note,60);
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_START).transport==BringUpAction::Exit);
    f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false);
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_START).transport==BringUpAction::PatternToggle);
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_B).transport==BringUpAction::ResetStop);
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_B).transport==BringUpAction::ResetStop);
    f.Tap(SDL_CONTROLLER_BUTTON_X); CHECK_EQ(f.editor.Draft()->cells[0][0].note,NOTE_OFF);
    f.Tap(SDL_CONTROLLER_BUTTON_Y); CHECK_EQ(f.editor.Draft()->cells[0][0].note,NOTE_EMPTY); CHECK_EQ(f.editor.Draft()->cells[0][0].instrument,255);
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_A).publish);
    CHECK(!f.Tap(SDL_CONTROLLER_BUTTON_BACK).publish); // Select unassigned.
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER).transport==BringUpAction::None);
}
TEST_CASE(ArkOSSharedValueDirectionRepeatAndOffTiming) {
    for(const auto direction:{ValueStepDirection::Normal,ValueStepDirection::Reversed}) {
        InputFixture f(direction); f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP); CHECK_EQ(f.editor.Draft()->cells[0][0].note,direction==ValueStepDirection::Normal?61:59);
        f.input.Tick(399,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,direction==ValueStepDirection::Normal?61:59);
        f.input.Tick(400,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,direction==ValueStepDirection::Normal?62:58);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP,false); f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false);
        f.Tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); CHECK(f.editor.Cursor().field==EditField::Instrument);
        f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); f.Tap(SDL_CONTROLLER_BUTTON_DPAD_UP);
        CHECK_EQ(f.editor.Draft()->cells[0][0].instrument,direction==ValueStepDirection::Normal?1:255);
        f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false); f.Tap(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        f.Tap(SDL_CONTROLLER_BUTTON_X); f.Tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); f.Tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        CHECK(f.editor.OffTiming(0,0)==NoteOffTiming::EndOfPosition);
        f.Tap(SDL_CONTROLLER_BUTTON_DPAD_UP); CHECK(f.editor.OffTiming(0,0)==NoteOffTiming::Arrival);
    }
    InputFixture f; CHECK(f.editor.Apply(EditorAction::SetValue,126)); f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP); CHECK_EQ(f.editor.Draft()->cells[0][0].note,127);
    f.input.Tick(400,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,NOTE_OFF);
    f.input.Tick(799,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,NOTE_OFF);
    f.input.Tick(800,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,24);
}
TEST_CASE(ArkOSReleaseOrderingOppositionFocusDisconnectAndLock) {
    InputFixture f;
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN); CHECK_EQ(f.editor.Cursor().row,1);
    f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); f.input.Tick(500,true,f.editor);
    CHECK_EQ(f.editor.Draft()->cells[1][0].note,NOTE_EMPTY); // Modifier must not rearm held navigation.
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    CHECK_EQ(f.editor.Draft()->cells[1][0].note,60);
    f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false); f.input.Tick(1000,true,f.editor);
    CHECK_EQ(f.editor.Cursor().row,1); CHECK_EQ(f.editor.Draft()->cells[1][0].note,60);
    f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); f.input.Tick(1500,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[1][0].note,60);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP);
    CHECK_EQ(f.editor.Draft()->cells[1][0].note,61);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN); f.input.Tick(2000,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[1][0].note,61);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.input.Tick(2500,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[1][0].note,61);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP,false); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP);
    CHECK_EQ(f.editor.Draft()->cells[1][0].note,62);
    f.editor.SetLocked(true); f.input.Tick(3000,true,f.editor); f.editor.SetLocked(false);
    f.input.Tick(3500,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[1][0].note,62);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP,false); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP);
    f.input.Tick(4000,false,f.editor); f.input.Tick(4500,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[1][0].note,63);
    SDL_Event focus{}; focus.type=SDL_WINDOWEVENT; focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    f.input.Handle(focus,4600,true,f.editor); focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;
    f.input.Handle(focus,4700,true,f.editor); f.input.Tick(5000,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[1][0].note,63);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP,false); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP);
    SDL_Event removal{}; removal.type=SDL_CONTROLLERDEVICEREMOVED; removal.cdevice.which=7;
    f.input.Handle(removal,5100,true,f.editor); CHECK_EQ(f.input.Device(),-1);
    f.input.Tick(6000,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[1][0].note,64);
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_START).transport==BringUpAction::None);
}
TEST_CASE(ArkOSIdentityAndVerifiedRawFallbackAvoidDuplicates) {
    InputFixture f;
    CHECK(f.Edge(SDL_CONTROLLER_BUTTON_START,true,8).transport==BringUpAction::None);
    SDL_Event raw{}; raw.type=SDL_JOYBUTTONDOWN; raw.jbutton.which=7; raw.jbutton.button=0;
    CHECK(f.input.Handle(raw,0,true,f.editor).transport==BringUpAction::None);
    SDL_GameControllerButtonBind bindings[SDL_CONTROLLER_BUTTON_MAX]{};
    bindings[SDL_CONTROLLER_BUTTON_B].bindType=SDL_CONTROLLER_BINDTYPE_BUTTON;
    bindings[SDL_CONTROLLER_BUTTON_B].value.button=9;
    const unsigned masks[]={SDL_HAT_UP,SDL_HAT_DOWN,SDL_HAT_LEFT,SDL_HAT_RIGHT};
    for(unsigned i=0;i<4;++i) { auto& b=bindings[SDL_CONTROLLER_BUTTON_DPAD_UP+i]; b.bindType=SDL_CONTROLLER_BINDTYPE_HAT; b.value.hat.hat=0; b.value.hat.hat_mask=masks[i]; }
    f.input.Select(7,ArkOSInputStream::VerifiedRaw,bindings);
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_B).transport==BringUpAction::None);
    raw.jbutton.button=9; CHECK(f.input.Handle(raw,0,true,f.editor).transport==BringUpAction::ResetStop);
    CHECK(f.input.Handle(raw,0,true,f.editor).transport==BringUpAction::None);
    raw.type=SDL_JOYBUTTONUP; f.input.Handle(raw,0,true,f.editor);
    raw.type=SDL_JOYHATMOTION; raw.jhat.which=7; raw.jhat.hat=0; raw.jhat.value=SDL_HAT_DOWN;
    f.input.Handle(raw,0,true,f.editor); CHECK_EQ(f.editor.Cursor().row,1);
    f.input.Handle(raw,0,true,f.editor); CHECK_EQ(f.editor.Cursor().row,1);
    raw.jhat.value=SDL_HAT_UP; f.input.Handle(raw,0,true,f.editor); CHECK_EQ(f.editor.Cursor().row,0);
    raw.jhat.which=8; raw.jhat.value=SDL_HAT_DOWN; f.input.Handle(raw,0,true,f.editor); CHECK_EQ(f.editor.Cursor().row,0);
}
TEST_CASE(ArkOSPublicationUsesExistingCapabilityGatesAndTransportStopExit) {
    auto* transport=new Transport; BringUpSerial serial(nullptr,std::unique_ptr<SerialTransport>(transport));
    BringUpControls controls(serial,nullptr); InputFixture f;
    controls.Tick(0); transport->rx="BTTEST1 STATE IDLE\n"; controls.Tick(1);
    CHECK(serial.Connected()); CHECK(!serial.Publish(f.editor,2)); // A without discovery does not bypass gates.
    const auto stop=f.Tap(SDL_CONTROLLER_BUTTON_B); controls.Request(stop.transport,"B",3); controls.Tick(4);
    CHECK(transport->tx.find("BTTEST1 STOP\n")!=std::string::npos);
    transport->rx="BTTEST1 STOPPED\n"; controls.Tick(5); CHECK(!controls.Quit());
    f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); const auto exit=f.Tap(SDL_CONTROLLER_BUTTON_START);
    CHECK(!exit.publish); controls.Request(exit.transport,"L1+Start",6); CHECK(controls.Quit());
}

TEST_CASE(ArkOSAUsesStoppedPausedCapabilityGatesWithoutChangingDraft) {
    for(const auto state:{"IDLE","PLAYING","PAUSED"}) for(const bool paused_cap:{false,true}) {
        auto* port=new Transport; BringUpSerial serial(nullptr,std::unique_ptr<SerialTransport>(port));
        InputFixture f; unsigned now=0;
        auto tick=[&](){serial.Tick(now++);};
        tick(); tick();
        port->rx=std::string("BTTEST1 STATE ")+state+"\nBTPATTERN1 SNAPCAP 2\nBTPUBLISH1 CAP 1\nBTPUBLISH1 NEXT 1 1\n"+
            "BTPATTERN1 POS 2 "+std::string(!std::strcmp(state,"PLAYING")?"1":"0")+" 0 0 0 0 32 2 "+
            (!std::strcmp(state,"PAUSED")?"1":"0")+"\n"+(paused_cap?"BTPUBLISH1 PAUSEDCAP 1\n":"");
        for(unsigned i=0;i<10;++i) tick();
        // Sender borrows pattern metadata, so retain its source throughout transfer.
        const auto source=PatternView()->device_pattern->pattern;
        PatternSnapshotSender stable(source,12753); CHECK(stable.Begin(1,0,2));
        while(stable.HasData()) {std::size_t n; const auto* line=stable.Data(n); port->rx.append(line,n); tick(); stable.Consume(n);}
        CHECK(f.editor.Apply(EditorAction::Increment)); const auto draft=*f.editor.Draft();
        CHECK(f.Tap(SDL_CONTROLLER_BUTTON_A).publish);
        const bool accepted=serial.Publish(f.editor,now);
        CHECK_EQ(accepted,!std::strcmp(state,"IDLE") || (!std::strcmp(state,"PAUSED") && paused_cap));
        CHECK(SamePattern(draft,*f.editor.Draft()));
        CHECK(!serial.StartPending());
    }
}

TEST_CASE(ArkOSActiveCleanExitWaitsForStopAndPreservesDraft) {
    auto* port=new Transport; BringUpSerial serial(nullptr,std::unique_ptr<SerialTransport>(port));
    BringUpControls controls(serial,nullptr); InputFixture f;
    controls.Tick(0); port->rx="BTTEST1 STATE PLAYING\n"; controls.Tick(1);
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN); f.Tap(SDL_CONTROLLER_BUTTON_X);
    const auto draft=*f.editor.Draft();
    f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); const auto result=f.Tap(SDL_CONTROLLER_BUTTON_START);
    controls.Request(result.transport,"L1+Start",2); CHECK(controls.Exiting()); CHECK(controls.StopPending()); CHECK(!controls.Quit());
    controls.Tick(3); CHECK(port->tx.find("BTTEST1 STOP\n")!=std::string::npos);
    CHECK(port->tx.find("BTTEST1 START\n")==std::string::npos); CHECK(SamePattern(draft,*f.editor.Draft()));
    port->rx="BTTEST1 STOPPED\n"; controls.Tick(4); CHECK(controls.Quit());
}

TEST_CASE(ArkOSRawHatMixedBindingTypesAndIndices) {
    InputFixture f;
    SDL_GameControllerButtonBind bindings[SDL_CONTROLLER_BUTTON_MAX]{};
    auto& up=bindings[SDL_CONTROLLER_BUTTON_DPAD_UP];
    up.bindType=SDL_CONTROLLER_BINDTYPE_HAT; up.value.hat.hat=0; up.value.hat.hat_mask=SDL_HAT_UP;
    auto& down=bindings[SDL_CONTROLLER_BUTTON_DPAD_DOWN];
    down.bindType=SDL_CONTROLLER_BINDTYPE_HAT; down.value.hat.hat=0; down.value.hat.hat_mask=SDL_HAT_DOWN;
    auto& left=bindings[SDL_CONTROLLER_BUTTON_DPAD_LEFT];
    left.bindType=SDL_CONTROLLER_BINDTYPE_BUTTON; left.value.button=0;
    // Right stays NONE; neither Button nor None may be interpreted as a hat.
    f.input.Select(7,ArkOSInputStream::VerifiedRaw,bindings);
    const auto draft=*f.editor.Draft();
    SDL_Event event{}; event.type=SDL_JOYHATMOTION; event.jhat.which=7; event.jhat.hat=0;
    event.jhat.value=SDL_HAT_DOWN; f.input.Handle(event,0,true,f.editor); CHECK_EQ(f.editor.Cursor().row,1);
    event.jhat.value=SDL_HAT_UP; f.input.Handle(event,1,true,f.editor); CHECK_EQ(f.editor.Cursor().row,0);
    // Down->Up must release first, otherwise opposing-direction protection blocks Up.
    event.jhat.value=SDL_HAT_RIGHT; f.input.Handle(event,2,true,f.editor);
    CHECK_EQ(f.editor.Cursor().channel,0); CHECK(f.editor.Cursor().field==EditField::Note);
    event.jhat.value=SDL_HAT_LEFT; f.input.Handle(event,3,true,f.editor);
    CHECK_EQ(f.editor.Cursor().channel,0); CHECK(f.editor.Cursor().field==EditField::Note);
    event.jhat.hat=1; event.jhat.value=SDL_HAT_DOWN; f.input.Handle(event,4,true,f.editor);
    CHECK_EQ(f.editor.Cursor().row,0); CHECK(SamePattern(draft,*f.editor.Draft()));
    event.type=SDL_JOYBUTTONDOWN; event.jbutton.which=7; event.jbutton.button=0;
    f.input.Handle(event,5,true,f.editor); // Button binding still works through its own event branch.
    CHECK_EQ(f.editor.Cursor().channel,0); CHECK(f.editor.Cursor().field==EditField::Note);
}

TEST_CASE(ArkOSNavigationRepeatThresholdsAndStalls) {
    InputFixture f; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN); CHECK_EQ(f.editor.Cursor().row,1);
    f.input.Tick(349,true,f.editor); CHECK_EQ(f.editor.Cursor().row,1);
    f.input.Tick(350,true,f.editor); CHECK_EQ(f.editor.Cursor().row,2);
    f.input.Tick(449,true,f.editor); CHECK_EQ(f.editor.Cursor().row,2);
    f.input.Tick(450,true,f.editor); CHECK_EQ(f.editor.Cursor().row,3);
    f.input.Tick(939,true,f.editor); CHECK_EQ(f.editor.Cursor().row,4); // One action after a stall.
    f.input.Tick(999,true,f.editor); CHECK_EQ(f.editor.Cursor().row,4); // Still 100 ms interval.
    f.input.Tick(1000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,5); // Now 60 ms.
    f.input.Tick(1059,true,f.editor); CHECK_EQ(f.editor.Cursor().row,5);
    f.input.Tick(1060,true,f.editor); CHECK_EQ(f.editor.Cursor().row,6);
    f.input.Tick(1959,true,f.editor); CHECK_EQ(f.editor.Cursor().row,7);
    f.input.Tick(1999,true,f.editor); CHECK_EQ(f.editor.Cursor().row,7);
    f.input.Tick(2000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,8); // Now 40 ms.
    f.input.Tick(2039,true,f.editor); CHECK_EQ(f.editor.Cursor().row,8);
    f.input.Tick(2040,true,f.editor); CHECK_EQ(f.editor.Cursor().row,9);
    f.input.Tick(6000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,10);
    f.input.Tick(6000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,10);
    f.input.Tick(6039,true,f.editor); CHECK_EQ(f.editor.Cursor().row,10);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.input.Tick(9000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,10);
    CHECK(!f.editor.Dirty()); CHECK(!f.editor.CanUndo());
}
TEST_CASE(ArkOSNavigationRepeatBoundsFieldsAndChannelWrapping) {
    InputFixture f; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    CHECK(f.editor.Cursor().field==EditField::Instrument);
    f.input.Tick(350,true,f.editor); CHECK_EQ(f.editor.Cursor().channel,1); CHECK(f.editor.Cursor().field==EditField::Note);
    f.input.Tick(450,true,f.editor); CHECK(f.editor.Cursor().field==EditField::Instrument);
    f.input.Tick(550,true,f.editor); CHECK_EQ(f.editor.Cursor().channel,1); CHECK(f.editor.Cursor().field==EditField::Instrument);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,false);
    f.now=600; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    f.input.Tick(950,true,f.editor); CHECK_EQ(f.editor.Cursor().channel,0); CHECK(f.editor.Cursor().field==EditField::Instrument);
    f.input.Tick(1050,true,f.editor); CHECK(f.editor.Cursor().field==EditField::Note);
    f.input.Tick(1150,true,f.editor); CHECK_EQ(f.editor.Cursor().channel,0); CHECK(f.editor.Cursor().field==EditField::Note);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_LEFT,false); f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    f.now=1200; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_LEFT); CHECK_EQ(f.editor.Cursor().channel,1);
    f.input.Tick(1550,true,f.editor); CHECK_EQ(f.editor.Cursor().channel,0); CHECK(f.editor.Cursor().field==EditField::Note);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_LEFT,false); f.now=1600; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    CHECK_EQ(f.editor.Cursor().channel,1); f.input.Tick(1950,true,f.editor); CHECK_EQ(f.editor.Cursor().channel,0);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,false); f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false);
    for(unsigned row=0;row<31;++row) f.Tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    f.now=2000; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN); f.input.Tick(2350,true,f.editor); CHECK_EQ(f.editor.Cursor().row,31);
    CHECK(!f.editor.Dirty());
}
TEST_CASE(ArkOSNavigationRepeatLockModifierOppositionAndSingleStream) {
    InputFixture f; f.editor.SetLocked(true); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    f.input.Tick(350,true,f.editor); CHECK_EQ(f.editor.Cursor().row,2);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    f.now=500; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); f.input.Tick(850,true,f.editor);
    CHECK_EQ(f.editor.Cursor().channel,0); CHECK(f.editor.Cursor().field==EditField::Note);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,false); f.now=900; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP);
    f.input.Tick(1300,true,f.editor); CHECK_EQ(f.editor.Cursor().row,2); CHECK_EQ(f.editor.Draft()->cells[2][0].note,NOTE_EMPTY);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP,false); f.editor.SetLocked(false);
    f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false); f.now=1400; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    CHECK_EQ(f.editor.Cursor().row,3); f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    f.input.Tick(2000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,3); CHECK_EQ(f.editor.Draft()->cells[3][0].note,NOTE_EMPTY);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.now=2100; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    CHECK_EQ(f.editor.Draft()->cells[3][0].note,60);
    f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false); f.input.Tick(2600,true,f.editor);
    CHECK_EQ(f.editor.Cursor().row,3); CHECK_EQ(f.editor.Draft()->cells[3][0].note,60);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.now=2700; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    CHECK_EQ(f.editor.Cursor().row,4); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP);
    f.input.Tick(3100,true,f.editor); CHECK_EQ(f.editor.Cursor().row,4);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP,false); f.input.Tick(3500,true,f.editor); CHECK_EQ(f.editor.Cursor().row,4);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.now=3600; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    CHECK_EQ(f.editor.Cursor().row,5); f.editor.SetLocked(true); f.input.Tick(3950,true,f.editor); CHECK_EQ(f.editor.Cursor().row,6);
}
TEST_CASE(ArkOSNavigationRepeatCancellationAndTimestampWrap) {
    for(unsigned scenario=0;scenario<5;++scenario) {
        InputFixture f; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        SDL_Event event{};
        if(scenario==0) {event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;f.input.Handle(event,100,true,f.editor);
            event.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;f.input.Handle(event,200,true,f.editor);}
        if(scenario==1) {event.type=SDL_CONTROLLERDEVICEREMOVED;event.cdevice.which=7;f.input.Handle(event,100,true,f.editor);}
        if(scenario==2) f.input.Tick(100,false,f.editor);
        if(scenario==3) {f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); CHECK(f.Tap(SDL_CONTROLLER_BUTTON_START).transport==BringUpAction::Exit);}
        if(scenario==4) {event.type=SDL_QUIT;CHECK(f.input.Handle(event,100,true,f.editor).transport==BringUpAction::Exit);}
        f.input.Tick(1000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,1);
    }
    InputFixture f; f.now=UINT32_MAX-100; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    f.input.Tick(248,true,f.editor); CHECK_EQ(f.editor.Cursor().row,1);
    f.input.Tick(249,true,f.editor); CHECK_EQ(f.editor.Cursor().row,2);
}

TEST_CASE(ArkOSNavigationAndValueStreamReplacementPreservesValueSchedule) {
    InputFixture f; f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP); CHECK_EQ(f.editor.Draft()->cells[0][0].note,61);
    f.now=10; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); CHECK_EQ(f.editor.Cursor().channel,1);
    f.input.Tick(359,true,f.editor); CHECK_EQ(f.editor.Cursor().channel,1);
    f.input.Tick(360,true,f.editor); CHECK_EQ(f.editor.Cursor().channel,0);
    f.input.Tick(400,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,61);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,false); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP,false);
    f.now=500; f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP); CHECK_EQ(f.editor.Draft()->cells[0][0].note,62);
    f.input.Tick(850,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,62);
    f.input.Tick(899,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,62);
    f.input.Tick(900,true,f.editor); CHECK_EQ(f.editor.Draft()->cells[0][0].note,63);
    CHECK_EQ(f.editor.Cursor().channel,0);
}

TEST_CASE(ArkOSTempoChordBothModifierOrdersRepeatOppositesReleaseAndRemoval) {
    for(auto direction:{ValueStepDirection::Normal,ValueStepDirection::Reversed}) {
        InputFixture f(direction); const auto pattern=*f.editor.Draft(); const auto cursor=f.editor.Cursor();
        f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        CHECK_EQ(f.input.TakeTempoStep(),5);CHECK_EQ(f.editor.Cursor().channel,cursor.channel);CHECK_EQ(f.editor.Cursor().field,cursor.field);
        CHECK(SamePattern(pattern,*f.editor.Draft()));
        f.input.Tick(400,true,f.editor);CHECK_EQ(f.input.TakeTempoStep(),5);
        f.input.Tick(1500,true,f.editor);CHECK_EQ(f.input.TakeTempoStep(),25);
        f.input.Tick(3000,true,f.editor);CHECK_EQ(f.input.TakeTempoStep(),100);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_LEFT);CHECK_EQ(f.input.TakeTempoStep(),0);
        f.input.Tick(4000,true,f.editor);CHECK_EQ(f.input.TakeTempoStep(),0);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_LEFT,false); f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,false);
        f.input.Tick(5000,true,f.editor);CHECK_EQ(f.editor.Cursor().channel,cursor.channel);CHECK_EQ(f.editor.Cursor().field,cursor.field);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,false);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_LEFT);const auto before=f.editor.Cursor(); // navigation before the modifier is pressed
        f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);CHECK_EQ(f.input.TakeTempoStep(),-5);
        f.input.Tick(5400,true,f.editor);CHECK_EQ(f.editor.Cursor().channel,before.channel);CHECK_EQ(f.editor.Cursor().field,before.field);
        SDL_Event removed{};removed.type=SDL_CONTROLLERDEVICEREMOVED;removed.cdevice.which=7;
        f.input.Handle(removed,5500,true,f.editor);f.input.Tick(6000,true,f.editor);CHECK_EQ(f.input.TakeTempoStep(),0);
    }
}
TEST_CASE(ArkOSTempoVerifiedRawButtonsAndHatConsumeChord) {
    InputFixture f; SDL_GameControllerButtonBind bindings[SDL_CONTROLLER_BUTTON_MAX]{};
    bindings[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER].bindType=SDL_CONTROLLER_BINDTYPE_BUTTON;
    bindings[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER].value.button=4;
    for(auto b:{SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT}) {
        bindings[b].bindType=SDL_CONTROLLER_BINDTYPE_HAT;bindings[b].value.hat.hat=0;
        bindings[b].value.hat.hat_mask=b==SDL_CONTROLLER_BUTTON_DPAD_LEFT?SDL_HAT_LEFT:SDL_HAT_RIGHT;
    }
    f.input.Select(7,ArkOSInputStream::VerifiedRaw,bindings);const auto before=f.editor.Cursor();
    SDL_Event e{};e.type=SDL_JOYBUTTONDOWN;e.jbutton.which=7;e.jbutton.button=4;f.input.Handle(e,0,true,f.editor);
    e={};e.type=SDL_JOYHATMOTION;e.jhat.which=7;e.jhat.hat=0;e.jhat.value=SDL_HAT_RIGHT;
    f.input.Handle(e,1,true,f.editor);CHECK_EQ(f.input.TakeTempoStep(),5);
    f.input.Tick(401,true,f.editor);CHECK_EQ(f.input.TakeTempoStep(),5);
    CHECK_EQ(f.editor.Cursor().channel,before.channel);CHECK_EQ(f.editor.Cursor().field,before.field);
    e={};e.type=SDL_JOYBUTTONUP;e.jbutton.which=7;e.jbutton.button=4;f.input.Handle(e,402,true,f.editor);
    f.input.Tick(1000,true,f.editor);CHECK_EQ(f.input.TakeTempoStep(),0);CHECK_EQ(f.editor.Cursor().field,before.field);
}

TEST_CASE(ArkOSBeatChordsRepeatModifierOrdersAndCancellation) {
    for (auto direction : {ValueStepDirection::Normal, ValueStepDirection::Reversed}) {
        InputFixture f(direction); f.Tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        const auto pattern=*f.editor.Draft();
        f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        CHECK_EQ(f.editor.Cursor().row,4); CHECK_EQ(f.input.TakeTempoStep(),0);
        f.input.Tick(349,true,f.editor); CHECK_EQ(f.editor.Cursor().row,4);
        f.input.Tick(350,true,f.editor); CHECK_EQ(f.editor.Cursor().row,8);
        f.input.Tick(450,true,f.editor); CHECK_EQ(f.editor.Cursor().row,12);
        f.input.Tick(1000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,16);
        f.input.Tick(1059,true,f.editor); CHECK_EQ(f.editor.Cursor().row,16);
        f.input.Tick(1060,true,f.editor); CHECK_EQ(f.editor.Cursor().row,20);
        f.input.Tick(2000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,24);
        f.input.Tick(2040,true,f.editor); CHECK_EQ(f.editor.Cursor().row,28);
        f.input.Tick(10000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,28);
        f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,false); f.input.Tick(11000,true,f.editor);
        CHECK_EQ(f.editor.Cursor().row,28);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP);
        CHECK_EQ(f.editor.Cursor().row,27); // Plain navigation before modifier.
        f.now=12000; f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK_EQ(f.editor.Cursor().row,24);
        f.input.Tick(12350,true,f.editor); CHECK_EQ(f.editor.Cursor().row,20);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN); f.input.Tick(13000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,20);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false); f.input.Tick(14000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,20);
        f.Edge(SDL_CONTROLLER_BUTTON_DPAD_UP,false); f.Tap(SDL_CONTROLLER_BUTTON_DPAD_UP); CHECK_EQ(f.editor.Cursor().row,16);
        // R1 wins vertical chords over L1 editing, with no data or tempo change.
        f.Edge(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); f.Tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN); CHECK_EQ(f.editor.Cursor().row,20);
        CHECK_EQ(f.input.TakeTempoStep(),0); CHECK_EQ(f.editor.Cursor().channel,0);
        CHECK(f.editor.Cursor().field==EditField::Instrument); CHECK(SamePattern(pattern,*f.editor.Draft()));
        CHECK(!f.editor.Dirty()); CHECK(!f.editor.CanUndo());
    }
    for (unsigned scenario=0; scenario<5; ++scenario) {
        InputFixture f; f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        SDL_Event e{};
        if(scenario==0) {e.type=SDL_WINDOWEVENT;e.window.event=SDL_WINDOWEVENT_FOCUS_LOST;f.input.Handle(e,100,true,f.editor);
            e.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;f.input.Handle(e,200,true,f.editor);}
        if(scenario==1) {e.type=SDL_CONTROLLERDEVICEREMOVED;e.cdevice.which=7;f.input.Handle(e,100,true,f.editor);}
        if(scenario==2) f.input.Tick(100,false,f.editor);
        if(scenario==3) f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,false);
        if(scenario==4) f.Edge(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false);
        f.input.Tick(5000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,4); CHECK_EQ(f.input.TakeTempoStep(),0);
    }
}
TEST_CASE(ArkOSBeatVerifiedRawButtonAndHatPaths) {
    for (bool hat : {false,true}) {
        InputFixture f; SDL_GameControllerButtonBind bindings[SDL_CONTROLLER_BUTTON_MAX]{};
        bindings[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER].bindType=SDL_CONTROLLER_BINDTYPE_BUTTON;
        bindings[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER].value.button=4;
        for(auto b:{SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN}) {
            bindings[b].bindType=hat?SDL_CONTROLLER_BINDTYPE_HAT:SDL_CONTROLLER_BINDTYPE_BUTTON;
            if(hat) {bindings[b].value.hat.hat=0;bindings[b].value.hat.hat_mask=b==SDL_CONTROLLER_BUTTON_DPAD_UP?SDL_HAT_UP:SDL_HAT_DOWN;}
            else bindings[b].value.button=b==SDL_CONTROLLER_BUTTON_DPAD_UP?5:6;
        }
        f.input.Select(7,ArkOSInputStream::VerifiedRaw,bindings);
        const auto send=[&](bool modifier,bool down,unsigned now,bool up=false) {
            SDL_Event e{};
            if(modifier || !hat) {e.type=down?SDL_JOYBUTTONDOWN:SDL_JOYBUTTONUP;e.jbutton.which=7;e.jbutton.button=modifier?4:up?5:6;}
            else {e.type=SDL_JOYHATMOTION;e.jhat.which=7;e.jhat.hat=0;e.jhat.value=down?(up?SDL_HAT_UP:SDL_HAT_DOWN):SDL_HAT_CENTERED;}
            f.input.Handle(e,now,true,f.editor);
        };
        const auto before=*f.editor.Draft(); send(true,true,0); send(false,true,1);
        CHECK_EQ(f.editor.Cursor().row,4); f.input.Tick(351,true,f.editor); CHECK_EQ(f.editor.Cursor().row,8);
        send(true,false,352); f.input.Tick(1000,true,f.editor); CHECK_EQ(f.editor.Cursor().row,8);
        send(false,false,1001); send(false,true,1002,true); CHECK_EQ(f.editor.Cursor().row,7);
        send(true,true,1003); CHECK_EQ(f.editor.Cursor().row,4);
        CHECK_EQ(f.input.TakeTempoStep(),0); CHECK(SamePattern(before,*f.editor.Draft()));
        SDL_Event removed{}; removed.type=SDL_JOYDEVICEREMOVED;removed.jdevice.which=7;
        f.input.Handle(removed,1100,true,f.editor); f.input.Tick(1500,true,f.editor); CHECK_EQ(f.editor.Cursor().row,4);
    }
}

TEST_CASE(ArkOSHeaderAConsumesPublishAndRetainsTempoIncludingVerifiedRaw) {
    InputFixture f;
    f.Tap(SDL_CONTROLLER_BUTTON_DPAD_UP);CHECK(f.editor.Cursor().header);
    const auto result=f.Tap(SDL_CONTROLLER_BUTTON_A);CHECK_EQ(result.mute_channel,0);CHECK(!result.publish);
    f.Edge(SDL_CONTROLLER_BUTTON_A);CHECK_EQ(f.Edge(SDL_CONTROLLER_BUTTON_A).mute_channel,-1);f.Edge(SDL_CONTROLLER_BUTTON_A,false);
    f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);CHECK_EQ(f.input.TakeTempoStep(),5);f.Edge(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,false);CHECK_EQ(f.editor.Cursor().channel,0);
    f.Edge(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,false);f.Tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);CHECK(!f.editor.Cursor().header);
    CHECK(f.Tap(SDL_CONTROLLER_BUTTON_A).publish);CHECK(!f.editor.Dirty());CHECK(!f.editor.CanUndo());
    SDL_GameControllerButtonBind bindings[SDL_CONTROLLER_BUTTON_MAX]{};
    bindings[SDL_CONTROLLER_BUTTON_A].bindType=SDL_CONTROLLER_BINDTYPE_BUTTON;bindings[SDL_CONTROLLER_BUTTON_A].value.button=9;
    f.input.Select(7,ArkOSInputStream::VerifiedRaw,bindings);CHECK(f.editor.SelectHeader(1));
    SDL_Event e{};e.type=SDL_JOYBUTTONDOWN;e.jbutton.which=7;e.jbutton.button=9;
    const auto raw=f.input.Handle(e,0,true,f.editor);CHECK_EQ(raw.mute_channel,1);CHECK(!raw.publish);
    CHECK_EQ(f.input.Handle(e,1,true,f.editor).mute_channel,-1);
}
