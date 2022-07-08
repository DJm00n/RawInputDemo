#pragma once

#include <Cfgmgr32.h>
#include <initguid.h>
#include <Devpkey.h>

inline std::vector<uint8_t> GetDeviceInterfaceProperty(const std::string& deviceInterfaceName, const DEVPROPKEY* propertyKey, DEVPROPTYPE expectedPropertyType)
{
    std::wstring devInterface = utf8::widen(deviceInterfaceName);
    DEVPROPTYPE propertyType = DEVPROP_TYPE_EMPTY;
    ULONG propertySize = 0;
    CONFIGRET cr = ::CM_Get_Device_Interface_PropertyW(devInterface.c_str(), propertyKey, &propertyType, nullptr, &propertySize, 0);

    if (cr == CR_NO_SUCH_VALUE)
        return {};

    DCHECK_EQ(cr, CR_BUFFER_SMALL);
    DCHECK_EQ(propertyType, expectedPropertyType);

    std::vector<uint8_t> propertyData(propertySize, 0);
    cr = ::CM_Get_Device_Interface_PropertyW(devInterface.c_str(), propertyKey, &propertyType, propertyData.data(), &propertySize, 0);

    DCHECK_EQ(cr, CR_SUCCESS);
    DCHECK_EQ(propertyType, expectedPropertyType);

    return propertyData;
}

inline DEVINST OpenDevNode(const std::string& deviceInstanceId)
{
    std::wstring devInstance = utf8::widen(deviceInstanceId);
    DEVINST devNodeHandle;
    CONFIGRET cr = ::CM_Locate_DevNodeW(&devNodeHandle, devInstance.data(), CM_LOCATE_DEVNODE_NORMAL);

    DCHECK_EQ(cr, CR_SUCCESS);

    return devNodeHandle;
}

inline std::vector<uint8_t> GetDevNodeProperty(DEVINST devInst, const DEVPROPKEY* propertyKey, DEVPROPTYPE expectedPropertyType)
{
    DEVPROPTYPE propertyType = DEVPROP_TYPE_EMPTY;
    ULONG propertySize = 0;
    CONFIGRET cr = ::CM_Get_DevNode_PropertyW(devInst, propertyKey, &propertyType, nullptr, &propertySize, 0);

    if (cr == CR_NO_SUCH_VALUE)
        return {};

    DCHECK_EQ(cr, CR_BUFFER_SMALL);
    DCHECK_EQ(propertyType, expectedPropertyType);

    std::vector<uint8_t> propertyData(propertySize, 0);
    cr = ::CM_Get_DevNode_PropertyW(devInst, propertyKey, &propertyType, propertyData.data(), &propertySize, 0);

    DCHECK_EQ(cr, CR_SUCCESS);
    DCHECK_EQ(propertyType, expectedPropertyType);

    return propertyData;
}

template<typename T> T PropertyDataCast(const std::vector<uint8_t>& propertyData)
{
    if (propertyData.empty())
        return {};

    return *const_cast<T*>(reinterpret_cast<const T*>(propertyData.data()));
}

template<> inline std::wstring PropertyDataCast(const std::vector<uint8_t>& propertyData)
{
    if (propertyData.empty())
        return {};

    const size_t len = propertyData.size() / sizeof(std::wstring::value_type) - 1;
    std::wstring wstr(reinterpret_cast<const std::wstring::value_type*>(propertyData.data()), len);

    return std::move(wstr);
}

template<> inline std::string PropertyDataCast(const std::vector<uint8_t>& propertyData)
{
    if (propertyData.empty())
        return {};

    std::wstring wstr(PropertyDataCast<std::wstring>(propertyData));

    return utf8::narrow(wstr.data(), wstr.size());
}

template<> inline std::vector<std::string> PropertyDataCast(const std::vector<uint8_t>& propertyData)
{
    std::string strList(PropertyDataCast<std::string>(propertyData));

    std::vector<std::string> outList;
    for (size_t i = 0; i != std::string::npos && i < strList.size(); i = strList.find('\0', i), ++i)
    {
        std::string elem(&strList[i]);
        if (!elem.empty())
            outList.emplace_back(elem);
    }

    return std::move(outList);
}

inline std::string GetDeviceInterface(const std::string& deviceInstanceId, LPCGUID intefaceGuid)
{
    std::wstring deviceID = utf8::widen(deviceInstanceId);
    ULONG listSize;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(&listSize, const_cast<LPGUID>(intefaceGuid), deviceID.data(), CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

    DCHECK(cr == CR_SUCCESS);

    std::vector<uint8_t> listData(listSize * sizeof(WCHAR), 0);
    cr = ::CM_Get_Device_Interface_ListW(const_cast<LPGUID>(intefaceGuid), deviceID.data(), reinterpret_cast<PZZWSTR>(listData.data()), listSize, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

    DCHECK(cr == CR_SUCCESS);

    auto interfaces = PropertyDataCast<std::vector<std::string>>(listData);

    if (interfaces.empty())
        return {};

    return interfaces.front();
}

inline std::string GetParentDevice(const std::string& deviceInstanceId)
{
    DEVINST devNodeHandle = OpenDevNode(deviceInstanceId);

    return PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Parent, DEVPROP_TYPE_STRING));
}

inline std::string SearchParentDeviceInterface(const std::string& deviceInstanceId, LPCGUID intefaceGuid)
{
    if (deviceInstanceId.empty())
        return {};

    std::string interfacePath = GetDeviceInterface(deviceInstanceId, intefaceGuid);

    if (interfacePath.empty())
    {
        std::string parentDeviceInstanceId = GetParentDevice(deviceInstanceId);
        while (!parentDeviceInstanceId.empty())
        {
            interfacePath = GetDeviceInterface(parentDeviceInstanceId, intefaceGuid);

            if (!interfacePath.empty())
                break;

            parentDeviceInstanceId = GetParentDevice(parentDeviceInstanceId);
        }
    }

    return interfacePath;
}

inline std::string GetDeviceFromInterface(const std::string& deviceInterfaceName)
{
    return PropertyDataCast<std::string>(
        GetDeviceInterfaceProperty(
            deviceInterfaceName,
            &DEVPKEY_Device_InstanceId,
            DEVPROP_TYPE_STRING));
}
