#include "NativeIDE/FIDEContent.h"
#include "FSourceIntellisense.h"
#include "Core/FAtomicFileWriter.h"

#include <ImGuiColorTextEdit/TextEditor.h>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <utility>

using namespace std::chrono_literals;

using namespace NativeIDE;

namespace {
    constexpr std::string_view Utf8Bom{"\xEF\xBB\xBF"};

    bool ReadSource(const std::filesystem::path& Path, std::string& Source) {
        std::ifstream File{Path, std::ios::binary | std::ios::ate};
        const std::streamoff Size{File.tellg()};

        if (Size < 0) {
            return false;
        }

        Source.resize(static_cast<std::size_t>(Size));
        return File.seekg(0) && File.read(Source.data(), Size);
    }

    std::string ConvertLineEndings(std::string_view Text, std::string_view LineEnding) {
        std::string Result{};
        Result.reserve(Text.size());

        while (!Text.empty()) {
            const auto End{Text.find_first_of("\r\n")};
            Result += Text.substr(0, End);

            if (End == std::string_view::npos) {
                break;
            }

            Result += LineEnding;
            Text.remove_prefix(End + (Text.substr(End).starts_with("\r\n") ? 2 : 1));
        }

        return Result;
    }
}

FIDEContent::FIDEContent(EIDELanguage Language, FValidator Validator, FBuildSavedHandler BuildSavedHandler)
	: mEditor{std::make_unique<TextEditor>()},
	  mIntellisense{std::make_unique<FSourceIntellisense>(*mEditor, Language, std::move(Validator))},
	  mFont{ImGui::GetIO().Fonts->AddFontDefaultVector()},
	  mBuildSavedHandler{std::move(BuildSavedHandler)} {
    constexpr char KoreanFont[]{"C:/Windows/Fonts/malgun.ttf"};
    std::error_code Error{};

    if (std::filesystem::is_regular_file(KoreanFont, Error)) {
        ImFontConfig Config{};
        Config.MergeMode = true;
        Config.DstFont = mFont;
        ImGui::GetIO().Fonts->AddFontFromFileTTF(KoreanFont, 0.0f, &Config);
    }

    mEditor->SetLanguage(TextEditor::LookupLanguage(Language == EIDELanguage::Lua ? "Lua" : "HLSL"));
    mEditor->SetTransactionCallback([this](const std::vector<TextEditor::Change>&) {
        auto Text{GetSourceText()};
        mDirty = Text != mSavedText;
        mUnbuilt = Text != mBuiltText;
        ++mRevision;
        if (!mDirty) {
            mSaveError.clear();
        }
        if (!mEditor->IsReadOnlyEnabled()) {
            mIntellisense->Edit(std::move(Text));
        }
    });
}

FIDEContent::~FIDEContent() = default;

bool FIDEContent::Open(const std::filesystem::path& Path, const std::filesystem::path& Workspace, bool ReadOnly) {
    std::string Source{};
    if (!ReadSource(Path, Source)) {
        return false;
    }

    std::string_view Code{Source};
    if (Code.starts_with(Utf8Bom)) {
        Code.remove_prefix(Utf8Bom.size());
    }

    mLineEnding = "\n";
    if (const auto FirstEnding{Code.find_first_of("\r\n")}; FirstEnding != std::string_view::npos && Code[FirstEnding] == '\r') {
        mLineEnding = Code.substr(FirstEnding).starts_with("\r\n") ? "\r\n" : "\r";
    }

    const bool Binary{Code.find('\0') != std::string_view::npos};
    mSavedText = Binary ? std::string{} : ConvertLineEndings(Code, "\n");
    mSavedSource = std::move(Source);
    mPath = Path;
    mEditor->SetText(mSavedText);
    mReadOnlySource = ReadOnly;
    mEditor->SetReadOnlyEnabled(ReadOnly || Binary || GetSourceText() != mSavedText);
    if (mEditor->IsReadOnlyEnabled()) {
        mIntellisense->Close();
    } else {
        mIntellisense->Open(Path, Workspace, mSavedText);
    }
    mEditor->SetFocus();
    mSaveError.clear();
    mBuiltText = mSavedText;
    mBuildOutput.clear();
    mCurrentBuild = false;
    mUnbuilt = false;
    mDirty = false;

    return true;
}

bool FIDEContent::Save() {
    if (!mDirty) {
        return true;
    }

    if (mEditor->IsReadOnlyEnabled()) {
        return false;
    }

    std::string DiskSource{};
    if (!ReadSource(mPath, DiskSource)) {
        mSaveError = "Cannot read the current source file.";
        return false;
    }

    if (DiskSource != mSavedSource) {
        mSaveError = "The source file changed on disk. Your edits are still open.";
        return false;
    }

    std::string Text{GetSourceText()};
    std::string Source{mSavedSource.starts_with(Utf8Bom) ? Utf8Bom : std::string_view{}};
    Source += ConvertLineEndings(Text, mLineEnding);

    Core::FAtomicFileWriter Writer{};
    if (!Writer.Open(mPath, mSaveError)) {
        return false;
    }

    Writer.GetStream().write(Source.data(), static_cast<std::streamsize>(Source.size()));
    if (!Writer.Commit(mSaveError)) {
        return false;
    }

    mSavedSource = std::move(Source);
    mSavedText = std::move(Text);
    mSaveError.clear();
    mDirty = false;
    mIntellisense->Save();

    return true;
}

void FIDEContent::Build() {
    if (mPath.empty() || mBuild.valid()) {
        return;
    }

    if (mEditor->IsReadOnlyEnabled()) {
        mBuildOutput = "Build unavailable for this read-only file.";

        return;
    }

    try {
        mBuild = mIntellisense->StartBuild();
    } catch (const std::exception& Error) {
        mBuildOutput = std::string{"Build failed.\n"} + Error.what();

        return;
    }

    if (!mBuild.valid()) {
        mBuildOutput = "Build failed. No shader compile target is registered for this file.";

        return;
    }

    mBuildRevision = mRevision;
    mCurrentBuild = true;
    mBuildOutput = "Building...";
    UpdateBuild();
}

void FIDEContent::UpdateBuild() {
    if (!mBuild.valid() || mBuild.wait_for(0s) != std::future_status::ready) {
        return;
    }

    const bool Current{std::exchange(mCurrentBuild, false)};
    FBuildResult Result{};
    try {
        Result = mBuild.get();
    } catch (const std::exception& Error) {
        Result.mOutput = Error.what();
    }

    if (!Current) {
        return;
    }

    if (mBuildRevision != mRevision) {
        mBuildOutput = "Source changed during the build. Press F5 to build the current text.";

        return;
    }

    mBuildOutput = std::move(Result.mOutput);
    if (!mBuildOutput.empty() && !mBuildOutput.ends_with('\n')) {
        mBuildOutput += '\n';
    }

    if (!Result.mSucceeded) {
        mBuildOutput += "Build failed. Changes remain in the editor.";

        return;
    }

    mBuiltText = GetSourceText();
    mUnbuilt = false;
    const bool Changed{mDirty};
    if (!Save()) {
        mBuildOutput += "Build succeeded. Save failed. " + mSaveError;

        return;
    }

    mBuildOutput += Changed ? "Build succeeded. File saved." : "Build succeeded.";
    if (mBuildSavedHandler) {
        mBuildOutput += '\n';
        mBuildOutput += mBuildSavedHandler();
    }
}

void FIDEContent::Close() {
    mIntellisense->Close();
    DiscardChanges();
    mCurrentBuild = false;
    mUnbuilt = false;
}

void FIDEContent::DiscardChanges() {
    if (mDirty) {
        mEditor->SetText(mSavedText);
        mDirty = false;
    }

    mSaveError.clear();
}

const std::filesystem::path& FIDEContent::GetPath() const {
    return mPath;
}

bool FIDEContent::IsDirty() const {
    return mDirty;
}

bool FIDEContent::HasUnbuiltChanges() const {
    return mUnbuilt;
}

void FIDEContent::Draw() {
    mIntellisense->Update();
    UpdateBuild();
    const bool ReadOnly{mEditor->IsReadOnlyEnabled()};
    if (ReadOnly) {
        ImGui::TextDisabled("%s", mReadOnlySource ? "Read-only engine shader source." : "Read-only: this file contains text the editor cannot preserve.");
    } else if (!mSaveError.empty()) {
        ImGui::TextWrapped("Save failed: %s", mSaveError.c_str());
    }

    const ImVec2 Available{ImGui::GetContentRegionAvail()};
    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const float SplitterHeight{std::max(4.0f, ImGui::GetFontSize() * 0.375f)};
    const float TotalHeight{std::max(2.0f, Available.y - SplitterHeight)};
    const float MinimumHeight{std::min(ImGui::GetFrameHeightWithSpacing() * 3.0f, TotalHeight * 0.5f)};
    float PanelHeight{std::clamp(TotalHeight * mBuildPanelRatio, MinimumHeight, TotalHeight - MinimumHeight)};
    float EditorHeight{TotalHeight - PanelHeight};
    const ImVec2 SplitterTop{Origin.x, Origin.y + EditorHeight};
    const ImVec2 SplitterBottom{Origin.x + std::max(1.0f, Available.x), SplitterTop.y + SplitterHeight};
    if (ImGui::SplitterBehavior(ImRect{SplitterTop, SplitterBottom}, ImGui::GetID("BuildMessagesSplitter"), ImGuiAxis_Y, &EditorHeight, &PanelHeight, MinimumHeight, MinimumHeight)) {
        mBuildPanelRatio = PanelHeight / TotalHeight;
    }

    ImGui::PushFont(mFont, 0.0f);
    mEditor->Render("##Source", {0.0f, EditorHeight});
    ImGui::PopFont();
    ImGui::SetCursorScreenPos({Origin.x, Origin.y + EditorHeight + SplitterHeight});
    DrawBuildOutput(PanelHeight);

    if (ReadOnly && mDirty) {
        DiscardChanges();
    }
}

void FIDEContent::DrawBuildOutput(float Height) {
    if (ImGui::BeginChild("##BuildMessages", {0.0f, Height}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Build Messages");
        ImGui::SameLine();
        ImGui::BeginDisabled(mBuild.valid() || mEditor->IsReadOnlyEnabled());
        if (ImGui::Button("Build (F5)")) {
            Build();
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::PushFont(mFont, 0.0f);
        ImGui::TextUnformatted(mBuildOutput.c_str());
        ImGui::PopFont();
    }
    ImGui::EndChild();
}

std::string FIDEContent::GetSourceText() const {
    return mEditor->GetSectionText({}, {mEditor->GetLineCount(), 0});
}
