#include "Core/FPathTracker.h"

#include <chrono>

using namespace std::chrono_literals;

using namespace Core;

FPathTracker::FPathTracker()
    : mWorker{[this](std::stop_token Stop) {
        Poll(Stop);
    }} {
}

FPathTracker::~FPathTracker() = default;

void FPathTracker::Track(const std::filesystem::path& Path) {
    const auto Stamp{ReadStamp(Path)};
    const std::lock_guard Lock{mMutex};
    mWatches.try_emplace(Path, FWatch{Stamp, ++mNextId, false});
}

void FPathTracker::Untrack(const std::filesystem::path& Path) {
    const std::lock_guard Lock{mMutex};
    mWatches.erase(Path);
}

std::vector<std::filesystem::path> FPathTracker::TakeChanges() {
    const std::lock_guard Lock{mMutex};
    std::vector<std::filesystem::path> Changes{};
    for (auto& [Path, Watch] : mWatches) {
        if (Watch.mChanged) {
            Changes.push_back(Path);
            Watch.mChanged = false;
        }
    }
    return Changes;
}

bool FPathTracker::FStamp::operator==(const FStamp& Other) const {
    return mWriteTime == Other.mWriteTime && mSize == Other.mSize;
}

std::optional<FPathTracker::FStamp> FPathTracker::ReadStamp(const std::filesystem::path& Path) {
    std::error_code Error{};
    const auto WriteTime{std::filesystem::last_write_time(Path, Error)};
    if (Error) {
        return {};
    }
    const auto Size{std::filesystem::file_size(Path, Error)};
    if (Error) {
        return {};
    }
    return FStamp{WriteTime, Size};
}

void FPathTracker::Poll(std::stop_token Stop) {
    std::unique_lock Lock{mMutex};
    while (!Stop.stop_requested()) {
        mWake.wait_for(Lock, Stop, 250ms, [] {
            return false;
        });
        if (Stop.stop_requested()) {
            break;
        }

        auto Sample{mWatches};
        Lock.unlock();
        for (auto& [Path, Watch] : Sample) {
            if (Stop.stop_requested()) {
                return;
            }
            Watch.mStamp = ReadStamp(Path);
        }

        Lock.lock();
        for (const auto& [Path, Watch] : Sample) {
            const auto Current{mWatches.find(Path)};
            if (Current != mWatches.end() && Current->second.mId == Watch.mId && Current->second.mStamp != Watch.mStamp) {
                Current->second.mStamp = Watch.mStamp;
                Current->second.mChanged = true;
            }
        }
    }
}
