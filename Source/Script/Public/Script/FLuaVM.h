#pragma once

#include "Core/Types.h"

#include <lua.hpp>
#include <sol/sol.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string_view>
#include <utility>

namespace Script {
    enum class ELuaCallback : std::size_t {
        BeginPlay,
        Tick,
        EndPlay,
        Overlap,
        EndOverlap,
        Hit,
        Count
    };

    class FLuaExecution {
    public:
        using FErrorHandler = std::function<void(const char*, std::string_view)>;

    public:
        template <typename... Args>
        bool Invoke(Script::ELuaCallback Callback, Args&&... Arguments) const;

        bool AdvanceTasks(float DeltaTime) const;

    private:
        friend class FLuaVM;

        static const char* GetCallbackName(Script::ELuaCallback Callback);
        bool FinishCall(const char* Stage, const sol::protected_function_result& Result) const;

    private:
        sol::environment mEnvironment{};
        sol::table mTasks{};
        sol::protected_function mAdvanceTasks{};
        Core::TStaticArray<sol::protected_function, static_cast<std::size_t>(Script::ELuaCallback::Count)> mCallbacks{};
        Script::FLuaExecution::FErrorHandler mErrorHandler{};
    };

    class FLuaVM {
    public:
        using FBindEnvironment = std::function<void(sol::environment&)>;

    public:
        FLuaVM();

    public:
        sol::state_view GetState();

        sol::protected_function LoadChunk(const std::filesystem::path& Path, const Script::FLuaExecution::FErrorHandler& ErrorHandler);
        bool CreateExecution(const sol::protected_function& Chunk, const Script::FLuaVM::FBindEnvironment& BindEnvironment, Script::FLuaExecution::FErrorHandler ErrorHandler, Script::FLuaExecution& Execution);

    private:
        sol::state mState{};
        sol::protected_function mMakeTasks{};
        sol::protected_function mCaptureCallbacks{};
    };

    template <typename... Args>
    bool Script::FLuaExecution::Invoke(Script::ELuaCallback Callback, Args&&... Arguments) const {
        const auto& Function{mCallbacks[static_cast<std::size_t>(Callback)]};

        return !Function.valid() || FinishCall(GetCallbackName(Callback), Function(std::forward<Args>(Arguments)...));
    }
}
