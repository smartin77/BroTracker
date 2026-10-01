// Read-only USB hub descriptor diagnostic. No CDC/audio opens or device changes.
// Build manually with the existing MinGW toolchain; not part of the package.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <initguid.h>
#include <setupapi.h>
#include <usbioctl.h>
#include <usbiodef.h>
#include <cstdio>
#include <vector>
#include <string>
#include <stdexcept>

std::vector<unsigned char> Descriptor(HANDLE hub, ULONG port, UCHAR index, USHORT language) {
    std::vector<unsigned char> bytes(sizeof(USB_DESCRIPTOR_REQUEST) + 255);
    auto* request = reinterpret_cast<USB_DESCRIPTOR_REQUEST*>(bytes.data());
    request->ConnectionIndex = port;
    request->SetupPacket.bmRequest = 0x80; request->SetupPacket.bRequest = 6;
    request->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | index;
    request->SetupPacket.wIndex = language; request->SetupPacket.wLength = 255;
    DWORD received = 0;
    if (!DeviceIoControl(hub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
            bytes.data(), DWORD(bytes.size()), bytes.data(), DWORD(bytes.size()), &received, nullptr))
        throw std::runtime_error("GET_DESCRIPTOR failed: Win32=" + std::to_string(GetLastError()));
    const size_t offset = sizeof(USB_DESCRIPTOR_REQUEST);
    if (received < offset + 2 || bytes[offset+1] != USB_STRING_DESCRIPTOR_TYPE ||
        bytes[offset] < 2 || (bytes[offset] & 1) || offset + bytes[offset] > received)
        throw std::runtime_error("Invalid USB string descriptor reply");
    return {bytes.begin()+offset, bytes.begin()+offset+bytes[offset]};
}
void PrintString(const char* label, const std::vector<unsigned char>& bytes) {
    std::wstring wide;
    for (size_t i=2; i<bytes.size(); i+=2) wide.push_back(wchar_t(bytes[i] | (bytes[i+1]<<8)));
    char utf8[1024]{};
    WideCharToMultiByte(CP_UTF8,0,wide.c_str(),-1,utf8,sizeof(utf8),nullptr,nullptr);
    std::printf("%s=%s\n%s_descriptor_hex=",label,utf8,label);
    for (auto b:bytes) std::printf("%02x", b);
    std::puts("");
}
int main() try {
    HDEVINFO devices=SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HUB,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if (devices==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate USB hubs");
    struct CloseSet { HDEVINFO h; ~CloseSet(){SetupDiDestroyDeviceInfoList(h);} } close_set{devices};
    unsigned found=0, inaccessible=0;
    for (DWORD i=0;;++i) {
        SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize=sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(devices,nullptr,&GUID_DEVINTERFACE_USB_HUB,i,&iface)) {
            if (GetLastError()!=ERROR_NO_MORE_ITEMS) throw std::runtime_error("Hub enumeration failed");
            break;
        }
        DWORD needed=0;
        SetupDiGetDeviceInterfaceDetailW(devices,&iface,nullptr,0,&needed,nullptr);
        if (!needed) { ++inaccessible; continue; }
        std::vector<unsigned char> bytes(needed);
        auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(bytes.data()); detail->cbSize=sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices,&iface,detail,needed,nullptr,nullptr)) {++inaccessible;continue;}
        HANDLE hub=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if (hub==INVALID_HANDLE_VALUE) {++inaccessible;continue;}
        struct CloseHub { HANDLE h; ~CloseHub(){CloseHandle(h);} } close_hub{hub};
        USB_NODE_INFORMATION node{}; node.NodeType=UsbHub; DWORD received=0;
        if (!DeviceIoControl(hub,IOCTL_USB_GET_NODE_INFORMATION,&node,sizeof(node),&node,sizeof(node),&received,nullptr)) {++inaccessible;continue;}
        for (ULONG port=1;port<=node.u.HubInformation.HubDescriptor.bNumberOfPorts;++port) {
            alignas(USB_NODE_CONNECTION_INFORMATION_EX) unsigned char info_bytes[4096]{};
            auto* info=reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(info_bytes); info->ConnectionIndex=port;
            if (!DeviceIoControl(hub,IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,info,sizeof(info_bytes),info,sizeof(info_bytes),&received,nullptr)) continue;
            if (info->ConnectionStatus!=DeviceConnected || info->DeviceDescriptor.idVendor!=0x16c0 || info->DeviceDescriptor.idProduct!=0x048a) continue;
            ++found;
            std::printf("USB match VID=16c0 PID=048a iProduct=%u iSerial=%u\n",info->DeviceDescriptor.iProduct,info->DeviceDescriptor.iSerialNumber);
            const auto languages=Descriptor(hub,port,0,0);
            if (languages.size()<4) throw std::runtime_error("No USB string language");
            const USHORT language=languages[2]|(languages[3]<<8);
            std::printf("GET_DESCRIPTOR language=0x%04x\n",language);
            PrintString("product",Descriptor(hub,port,info->DeviceDescriptor.iProduct,language));
            if (info->DeviceDescriptor.iSerialNumber) PrintString("serial",Descriptor(hub,port,info->DeviceDescriptor.iSerialNumber,language));
        }
    }
    std::printf("matches=%u inaccessible_hubs=%u\n",found,inaccessible);
    return found==1 ? 0 : 1;
} catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
