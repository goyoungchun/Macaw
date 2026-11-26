#include "Core/Log/OutputDeviceRedirector.h"
#include "Core/Types.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

namespace Core {
    namespace {
        constexpr Index_t BacklogCapacity{500};
        constexpr Core::TStaticArray<std::string_view, 8> VerbosityNames{"NoLogging", "Fatal", "Error", "Warning", "Display", "Log", "Verbose", "VeryVerbose"};

        class FDebugOutputDevice final : public Core::FOutputDevice {
        public:
            void Serialize(const Core::FLogRecord& Record) override;
        };

        class FFileOutputDevice final : public Core::FOutputDevice {
        public:
            void Serialize(const Core::FLogRecord& Record) override;

        private:
            std::mutex mMutex{};
            std::ofstream mFile{};
        };

        void FDebugOutputDevice::Serialize(const Core::FLogRecord& Record) {
            const Core::FString Line{Record.ToString() + '\n'};
            const int Length{MultiByteToWideChar(CP_UTF8, 0, Line.c_str(), -1, nullptr, 0)};

            if (Length == 0) {
                return;
            }

            std::wstring DebugLine{};
            DebugLine.resize(static_cast<Index_t>(Length));

            if (MultiByteToWideChar(CP_UTF8, 0, Line.c_str(), -1, DebugLine.data(), Length) != 0) {
                OutputDebugStringW(DebugLine.c_str());
            }
        }

        void FFileOutputDevice::Serialize(const Core::FLogRecord& Record) {
            const Core::FString Line{Record.ToString()};
            const std::scoped_lock Lock{mMutex};

            if (!mFile.is_open()) {
                std::error_code Error{};
                std::filesystem::create_directories("Log", Error);

                if (Error) {
                    return;
                }

                const auto Now{std::chrono::system_clock::now()};
                mFile.open(std::format("Log/runtime_{:%Y%m%d_%H%M%S}_{}.txt", Now, GetCurrentProcessId()), std::ios::out | std::ios::app);
            }

            if (mFile.is_open()) {
                mFile << Line << '\n';
                mFile.flush();
            }
        }
    }

    Core::FString FLogRecord::ToString() const {
        const Index_t VerbosityIndex{static_cast<Index_t>(mVerbosity)};
        const std::string_view Verbosity{VerbosityIndex < VerbosityNames.size() ? VerbosityNames[VerbosityIndex] : VerbosityNames[Core::ELogVerbosity::Log]};

        return std::format("{}: {}: {}", mCategory, Verbosity, mMessage);
    }

    FOutputDevice::~FOutputDevice() = default;

    FOutputDeviceRedirector::FOutputDeviceRedirector() {
        mOutputDevices.push_back(std::make_shared<FDebugOutputDevice>());
        mOutputDevices.push_back(std::make_shared<FFileOutputDevice>());
    }

    FOutputDeviceRedirector::~FOutputDeviceRedirector() = default;

    Core::FOutputDeviceRedirector* FOutputDeviceRedirector::Get() {
        static FOutputDeviceRedirector Redirector{};

        return &Redirector;
    }

    void FOutputDeviceRedirector::AddOutputDevice(const std::shared_ptr<Core::FOutputDevice>& Device) {
        const std::scoped_lock Lock{mMutex};

        if (Device != nullptr && Device.get() != this && std::ranges::find(mOutputDevices, Device) == mOutputDevices.end()) {
            mOutputDevices.push_back(Device);
        }
    }

    void FOutputDeviceRedirector::RemoveOutputDevice(const std::shared_ptr<Core::FOutputDevice>& Device) {
        const std::scoped_lock Lock{mMutex};

        std::erase(mOutputDevices, Device);
    }

    void FOutputDeviceRedirector::Serialize(const Core::FLogRecord& Record) {
        Core::TArray<std::shared_ptr<Core::FOutputDevice>> Devices{};

        {
            const std::scoped_lock Lock{mMutex};

            Devices = mOutputDevices;
            mBacklog.push_back(Record);
            ++mSequence;

            if (mBacklog.size() > BacklogCapacity) {
                mBacklog.pop_front();
            }
        }

        for (const auto& Device : Devices) {
            Device->Serialize(Record);
        }
    }

    Core::TArray<Core::FLogRecord> FOutputDeviceRedirector::ReadBacklog(Uint64& Sequence) const {
        const std::scoped_lock Lock{mMutex};

        if (Sequence >= mSequence) {
            return {};
        }

        const Uint64 Count{std::min(mSequence - Sequence, static_cast<Uint64>(mBacklog.size()))};
        Core::TArray<Core::FLogRecord> Records{mBacklog.end() - static_cast<std::ptrdiff_t>(Count), mBacklog.end()};
        Sequence = mSequence;

        return Records;
    }
}
