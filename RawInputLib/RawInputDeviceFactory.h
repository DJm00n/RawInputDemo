#pragma once

class RawInputDevice;

template<typename T>
class RawInputDeviceFactory
{
    friend class RawInputDeviceManager;

    std::unique_ptr<RawInputDevice> Create(HANDLE handle) const
    {
        return std::unique_ptr<T>(new T(handle));
    }
};

class RawInputDeviceHid;

template<>
class RawInputDeviceFactory<RawInputDeviceHid>
{
    friend class RawInputDeviceManager;

    std::unique_ptr<RawInputDevice> Create(HANDLE handle) const;
};