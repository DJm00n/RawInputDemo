#pragma once

class RawInputDevice;

template<typename T>
class RawInputDeviceFactory
{
    friend class RawInputDeviceManager;

    std::unique_ptr<RawInputDevice> Create(HANDLE handle) const
    {
        auto device = new T(handle);
		device->Initialize();

        return std::unique_ptr<T>(device);
    }
};

class RawInputDeviceHid;

template<>
class RawInputDeviceFactory<RawInputDeviceHid>
{
    friend class RawInputDeviceManager;

    std::unique_ptr<RawInputDevice> Create(HANDLE handle) const;
};