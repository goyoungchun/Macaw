#include "Script/FLuaVM.h"
#include "Core/Types.h"

#include "Core/FilePath.h"
#include "LuaBootstrap.generated.h"

#include <fstream>
#include <iterator>
#include <string>

#pragma comment(lib, "lua51.lib")

using namespace Script;

namespace {
    constexpr Core::TStaticArray<const char*, static_cast<std::size_t>(ELuaCallback::Count)> CallbackNames{"BeginPlay", "Tick", "EndPlay", "OnOverlap", "OnEndOverlap", "OnHit"};
}

const char* FLuaExecution::GetCallbackName(ELuaCallback Callback) {
    return CallbackNames[static_cast<std::size_t>(Callback)];
}

bool FLuaExecution::AdvanceTasks(float DeltaTime) const {
    return !mAdvanceTasks.valid() || FinishCall("Coroutine", mAdvanceTasks(mTasks, DeltaTime));
}

bool FLuaExecution::FinishCall(const char* Stage, const sol::protected_function_result& Result) const {
    if (!Result.valid()) {
        const auto Error{Result.get<sol::error>()};
        mErrorHandler(Stage, Error.what());
        return false;
    }
    return true;
}

FLuaVM::FLuaVM() {
    mState.open_libraries(sol::lib::base, sol::lib::coroutine, sol::lib::table, sol::lib::string, sol::lib::math, sol::lib::jit);

    const std::string_view Source{reinterpret_cast<const char*>(Script::LuaBootstrapSource), sizeof(Script::LuaBootstrapSource)};
    const auto Bootstrap{mState.script(Source, "@Source/Script/Private/Lua/Bootstrap.lua")};
    const sol::table Runtime{Bootstrap.get<sol::table>()};
    mMakeTasks = Runtime["MakeTasks"];
    mCaptureCallbacks = Runtime["CaptureCallbacks"];
}

sol::state_view FLuaVM::GetState() {
    return sol::state_view{mState.lua_state()};
}

sol::protected_function FLuaVM::LoadChunk(const std::filesystem::path& Path, const FLuaExecution::FErrorHandler& ErrorHandler) {
    std::ifstream File{Path, std::ios::binary};
    if (!File) {
        ErrorHandler("Load", "Could not open the script");
        return {};
    }

    const std::string Buffer{std::istreambuf_iterator<char>{File}, std::istreambuf_iterator<char>{}};
    std::string_view Code{Buffer};
    if (Code.starts_with("\xEF\xBB\xBF")) {
        Code.remove_prefix(3);
    }

    const sol::load_result Loaded{mState.load(Code, "@" + Core::FilePath::ToGenericUtf8(Path), sol::load_mode::text)};
    if (!Loaded.valid()) {
        const auto Error{Loaded.get<sol::error>()};
        ErrorHandler("Compile", Error.what());
        return {};
    }

    return Loaded.get<sol::protected_function>();
}

bool FLuaVM::CreateExecution(const sol::protected_function& Chunk, const FBindEnvironment& BindEnvironment, FLuaExecution::FErrorHandler ErrorHandler, FLuaExecution& Execution) {
    if (!Chunk.valid()) {
        return false;
    }

    Execution.mErrorHandler = std::move(ErrorHandler);
    const auto Tasks{mMakeTasks()};
    if (!Execution.FinishCall("Create coroutine context", Tasks)) {
        return false;
    }
    Execution.mTasks = Tasks.get<sol::table>();
    Execution.mAdvanceTasks = Tasks.get<sol::protected_function>(1);

    Execution.mEnvironment = sol::environment{mState, sol::create, mState.globals()};
    Execution.mEnvironment["_G"] = Execution.mEnvironment;
    BindEnvironment(Execution.mEnvironment);
    Execution.mEnvironment["Tasks"] = Execution.mTasks;

    const auto PreviousEnvironment{sol::get_environment(Chunk)};
    sol::set_environment(Execution.mEnvironment, Chunk);
    const auto Result{Chunk()};
    sol::set_environment(PreviousEnvironment, Chunk);
    if (!Execution.FinishCall("Initialize", Result)) {
        return false;
    }
    if (Result.return_count() != 0 && Result.get_type() == sol::type::function) {
        Execution.mErrorHandler("Initialize", "Define lifecycle callbacks directly in the script environment.");
        return false;
    }

    const auto Captured{mCaptureCallbacks(Execution.mEnvironment, sol::as_table(CallbackNames))};
    if (!Execution.FinishCall("Capture callbacks", Captured)) {
        return false;
    }

    const auto Callbacks{Captured.get<sol::table>()};
    for (std::size_t Index{}; Index < CallbackNames.size(); ++Index) {
        Execution.mCallbacks[Index] = Callbacks.get<sol::optional<sol::protected_function>>(CallbackNames[Index]).value_or(sol::protected_function{});
    }
    return true;
}
