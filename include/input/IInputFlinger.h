/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _LIBINPUT_IINPUT_FLINGER_H
#define _LIBINPUT_IINPUT_FLINGER_H

#include <stdint.h>
#include <sys/types.h>

#include <binder/IInterface.h>

#include <input/InputWindow.h>
#include <input/ISetInputWindowsListener.h>

namespace android {

/*
 * This class defines the Binder IPC interface for accessing various
 * InputFlinger features.
 */
class IInputFlinger : public IInterface {
public:
    DECLARE_META_INTERFACE(InputFlinger)

    virtual void setInputWindows(const std::vector<InputWindowInfo>& inputHandles,
            const sp<ISetInputWindowsListener>& setInputWindowsListener) = 0;
    virtual void registerInputChannel(const sp<InputChannel>& channel) = 0;
    virtual void unregisterInputChannel(const sp<InputChannel>& channel) = 0;
};


/**
 * Binder implementation.
 */
class BnInputFlinger : public BnInterface<IInputFlinger> {
public:
    enum {
        SET_INPUT_WINDOWS_TRANSACTION = IBinder::FIRST_CALL_TRANSACTION,
        REGISTER_INPUT_CHANNEL_TRANSACTION,
        UNREGISTER_INPUT_CHANNEL_TRANSACTION
    };

    virtual status_t onTransact(uint32_t code, const Parcel& data,
            Parcel* reply, uint32_t flags = 0);
};

} // namespace android

#endif // _LIBINPUT_IINPUT_FLINGER_H
