#include "FParticleEditorToolkit.h"

#include "CoreUObject/Reflection/FArrayOps.h"
#include "CoreUObject/Reflection/FProperty.h"
#include "CoreUObject/Reflection/UClass.h"
#include "CoreUObject/Reflection/UScriptStruct.h"
#include "CoreUObject/UObject.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorToolkitHost.h"
#include "Editor/Widget/FEditorQuantityInputWidget.h"
#include "EditorStyle.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/UParticleEmitter.h"
#include "Engine/Particles/UParticleLODLevel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <utility>

using namespace Editor;

namespace {

    constexpr std::array CurveAxisColors{IM_COL32(238, 141, 137, 255), IM_COL32(126, 209, 164, 255), IM_COL32(127, 173, 242, 255)};

    bool EditText(const char* Label, Core::FString& Value) {
        std::array<char, 1024> Buffer{};

        if (Value.size() >= Buffer.size()) {
            ImGui::TextWrapped("%s", Value.c_str());
            return false;
        }

        std::ranges::copy(Value, Buffer.begin());

        if (!ImGui::InputText(Label, Buffer.data(), Buffer.size())) {
            return false;
        }

        Value = Buffer.data();
        return true;
    }

    bool EditVector(const char* Label, FVector& Value) {
        std::array<float, 3> Coordinates{Value.x, Value.y, Value.z};

        if (!ImGui::DragFloat3(Label, Coordinates.data(), 0.01f)) {
            return false;
        }

        const FVector Edited{Coordinates[0], Coordinates[1], Coordinates[2]};

        if (!Edited.IsFinite()) {
            return false;
        }

        Value = Edited;
        return true;
    }

    bool IsDistribution(const CoreUObject::FProperty& Property) {
        const auto* Struct{Property.GetStruct()};

        return Struct == Engine::FFloatDistribution::StaticStruct() || Struct == Engine::FVectorDistribution::StaticStruct();
    }

    bool IsCurveTrack(const CoreUObject::UObject& Object, const CoreUObject::FProperty& Property) {
        if (!IsDistribution(Property)) {
            return false;
        }

        const auto Value{Property.ReadValue(&Object)};
        const auto* Address{std::get<const void*>(Value)};
        const auto Mode{Property.GetStruct() == Engine::FFloatDistribution::StaticStruct() ? static_cast<const Engine::FFloatDistribution*>(Address)->mMode : static_cast<const Engine::FVectorDistribution*>(Address)->mMode};
        return Mode == Engine::EDistributionMode::Curve;
    }

    Core::FString PropertyLabel(std::string_view Name) {
        Core::FString Label{};

        for (Index_t Index{}; Index < Name.size(); ++Index) {
            const char Character{Name[Index]};
            if (Index > 0 && Character >= 'A' && Character <= 'Z' && Name[Index - 1] >= 'a' && Name[Index - 1] <= 'z') {
                Label += ' ';
            }
            Label += Character;
        }

        return Label;
    }

    bool EditDistributionMode(Engine::EDistributionMode& Mode) {
        int Selection{static_cast<int>(Mode)};

        EditorStyle::DrawFieldLabel("Mode");
        if (!ImGui::Combo("##Mode", &Selection, "Constant\0Uniform\0Curve\0")) {
            return false;
        }

        Mode = static_cast<Engine::EDistributionMode>(Selection);
        return true;
    }

}

void FParticleEditorToolkit::DrawDetails() {
    EditorStyle::DrawFieldLabel("Filter", ImGui::CalcTextSize("Filter").x);
    ImGui::InputTextWithHint("##DetailsFilter", "Property name", mDetailsFilter.data(), mDetailsFilter.size(), ImGuiInputTextFlags_EscapeClearsAll);
    const ImGuiTextFilter Filter{mDetailsFilter.data()};
    auto* Object{GetSelectedObject()};

    if (Object == nullptr) {
        ImGui::TextUnformatted("Select an emitter or module.");
        return;
    }

    ImGui::BeginDisabled(!mContext.CanEditWorld());
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));

    if (mSelection == ESelection::Emitter && Filter.PassFilter("Emitter Name")) {
        ImGui::SeparatorText("Emitter");
        EditorStyle::DrawFieldLabel("Name");
        Core::FString Name{Object->GetName()};

        if (EditText("##EmitterName", Name) && Object->SetName(Name)) {
            MarkDirty();
        }
    }

    std::optional<Core::FString> Category{};
    bool CategoryOpen{};
    bool HasProperties{};

    for (const auto* Property : Object->GetClass()->GetAllProperties()) {
        if (!Property->HasFlag(Core::EPropertyFlags::Edit) || Property->HasFlag(Core::EPropertyFlags::ReadOnly) || Property->GetName() == "Name") {
            continue;
        }

        const auto& Next{Property->GetMetaData("Category")};
        const auto Label{PropertyLabel(Property->GetName())};
        if (!Filter.PassFilter((Label + " " + Next).c_str())) {
            continue;
        }

        HasProperties = true;
        if (Category != Next) {
            Category = Next;
            CategoryOpen = ImGui::CollapsingHeader(Category->empty() ? "Properties" : Category->c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        }
        if (!CategoryOpen) {
            continue;
        }

        ImGui::PushID(Property);
        if (mSelection == ESelection::System && Property->GetName() == "LODDistances") {
            const Core::FString Distance{"LOD " + std::to_string(mLOD) + " Distance"};
            EditorStyle::DrawFieldLabel(Distance.c_str());
            DrawLODDistance();
        } else if (DrawProperty(Object, *Property)) {
            if (mSelection == ESelection::Required && Property->GetName() == "UseLocalSpace") {
                const auto* Required{GetLevel()->GetRequiredModule()};

                for (const auto& Level : GetEmitter()->GetLODLevels()) {
                    Level->GetRequiredModule()->SetUseLocalSpace(Required->IsUseLocalSpace());
                }
            }

            MarkDirty();
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    if (!HasProperties && (mSelection != ESelection::Emitter || !Filter.PassFilter("Emitter Name"))) {
        ImGui::TextDisabled(Filter.IsActive() ? "No matching properties." : "No editable properties.");
    }

    ImGui::PopStyleColor();
    ImGui::EndDisabled();
}

bool FParticleEditorToolkit::DrawProperty(void* Object, const CoreUObject::FProperty& Property) {
    if (IsDistribution(Property)) {
        return DrawDistribution(Object, Property);
    }

    if (Property.GetArrayOps() != nullptr) {
        return DrawArray(Object, Property);
    }

    auto Value{Property.ReadValue(Object)};
    const Core::FString Label{PropertyLabel(Property.GetName())};
    constexpr const char* Field{"##Value"};
    bool Changed{};

    EditorStyle::DrawFieldLabel(Label.c_str());
    if (auto* Boolean{std::get_if<bool>(&Value)}) {
        Changed = ImGui::Checkbox(Field, Boolean);
    } else if (auto* Signed{std::get_if<Int32>(&Value)}) {
        const auto& Enums{Property.GetEnumValues()};

        if (Enums.empty()) {
            Changed = ImGui::DragInt(Field, Signed, 0.2f);
        } else {
            const auto Current{std::ranges::find_if(Enums, [Signed](const auto& Entry) {
                return Entry.mValue == *Signed;
            })};
            if (ImGui::BeginCombo(Field, Current != Enums.end() ? Current->mName.c_str() : "")) {
                for (const auto& Entry : Enums) {
                    if (ImGui::Selectable(Entry.mName.c_str(), Entry.mValue == *Signed)) {
                        *Signed = Entry.mValue;
                        Changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
    } else if (auto* Unsigned{std::get_if<Uint32>(&Value)}) {
        Changed = ImGui::DragScalar(Field, ImGuiDataType_U32, Unsigned, 0.2f);
    } else if (auto* Text{std::get_if<Core::FString>(&Value)}) {
        const std::string_view AssetType{Property.GetMetaData("AssetType")};

        if (AssetType == "Material" || AssetType == "Mesh") {
            const auto Type{AssetType == "Material" ? Asset::EAssetType::Material : Asset::EAssetType::Mesh};
            Changed = mPicker.Draw(mCatalog, mThumbnails, Field, *Text, Type, {}, "None", "Inherited", false);
        } else {
            Changed = EditText(Field, *Text);
        }
    } else if (auto* Vector{std::get_if<FVector>(&Value)}) {
        Changed = EditVector(Field, *Vector);
    } else if (auto* Quantity{std::get_if<CoreUObject::FQuantityPropertyValue>(&Value)}) {
        if (const auto Unit{Property.GetQuantityDisplayUnit(Quantity->mDimension)}) {
            Changed = FEditorQuantityInputWidget::EditValue(Field, Quantity->mBaseValue, *Unit);
        } else {
            ImGui::TextUnformatted("Invalid quantity unit");
        }
    } else if (auto* Number{std::get_if<float>(&Value)}) {
        if (Property.GetMetaData("DisplayUnit") == Core::QuantityUnits::Meter.mSymbol) {
            Changed = FEditorQuantityInputWidget::EditValue(Field, *Number, Core::QuantityUnits::Meter);
        } else {
            Changed = ImGui::DragFloat(Field, Number, 0.01f) && std::isfinite(*Number);
        }
    } else {
        ImGui::TextDisabled("-");
    }

    return Changed && Property.WriteValue(Object, Value);
}

bool FParticleEditorToolkit::DrawDistribution(void* Object, const CoreUObject::FProperty& Property) {
    if (!ImGui::TreeNodeEx(PropertyLabel(Property.GetName()).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }

    const auto Value{Property.ReadValue(Object)};
    const auto* Address{std::get<const void*>(Value)};
    const bool Scalar{Property.GetStruct() == Engine::FFloatDistribution::StaticStruct()};
    bool Changed{};
    Engine::EDistributionMode Mode{};

    if (Scalar) {
        auto Distribution{*static_cast<const Engine::FFloatDistribution*>(Address)};
        Changed = EditDistributionMode(Distribution.mMode);

        if (Distribution.mMode == Engine::EDistributionMode::Constant) {
            EditorStyle::DrawFieldLabel("Constant");
            Changed |= ImGui::DragFloat("##Constant", &Distribution.mConstant, 0.01f);
        } else if (Distribution.mMode == Engine::EDistributionMode::Uniform) {
            EditorStyle::DrawFieldLabel("Minimum");
            Changed |= ImGui::DragFloat("##Minimum", &Distribution.mMinValue, 0.01f);
            EditorStyle::DrawFieldLabel("Maximum");
            Changed |= ImGui::DragFloat("##Maximum", &Distribution.mMaxValue, 0.01f);
        }

        if (Distribution.mMode == Engine::EDistributionMode::Curve && Distribution.mCurveKeys.empty() && Changed) {
            Distribution.mCurveKeys.push_back({0, 0.0f, Distribution.mConstant});
            Distribution.mCurveKeys.push_back({1, 1.0f, Distribution.mConstant});
        }

        Mode = Distribution.mMode;
        Changed = Changed && std::isfinite(Distribution.mConstant) && std::isfinite(Distribution.mMinValue) && std::isfinite(Distribution.mMaxValue) && Property.WriteValue(Object, static_cast<const void*>(&Distribution));
    } else {
        auto Distribution{*static_cast<const Engine::FVectorDistribution*>(Address)};
        Changed = EditDistributionMode(Distribution.mMode);

        if (Distribution.mMode == Engine::EDistributionMode::Constant) {
            EditorStyle::DrawFieldLabel("Constant");
            Changed |= EditVector("##Constant", Distribution.mConstant);
        } else if (Distribution.mMode == Engine::EDistributionMode::Uniform) {
            EditorStyle::DrawFieldLabel("Minimum");
            Changed |= EditVector("##Minimum", Distribution.mMinValue);
            EditorStyle::DrawFieldLabel("Maximum");
            Changed |= EditVector("##Maximum", Distribution.mMaxValue);
            EditorStyle::DrawFieldLabel("Lock Axes");
            Changed |= ImGui::Checkbox("##LockAxes", &Distribution.mLockAxes);
        }

        if (Distribution.mMode == Engine::EDistributionMode::Curve && Changed) {
            const std::array Axes{&Distribution.mCurveKeysX, &Distribution.mCurveKeysY, &Distribution.mCurveKeysZ};
            const std::array Values{Distribution.mConstant.x, Distribution.mConstant.y, Distribution.mConstant.z};

            for (Index_t Index{}; Index < Axes.size(); ++Index) {
                if (Axes[Index]->empty()) {
                    Axes[Index]->push_back({0, 0.0f, Values[Index]});
                    Axes[Index]->push_back({1, 1.0f, Values[Index]});
                }
            }
        }

        Mode = Distribution.mMode;
        Changed = Changed && Property.WriteValue(Object, static_cast<const void*>(&Distribution));
    }

    if (Mode == Engine::EDistributionMode::Curve && ImGui::SmallButton("Edit Curve")) {
        ResetCurveSelection();
        mCurveProperty = &Property;
        GetToolkitHost().GetTabManager().TryInvokeTab("Curves");
    }

    ImGui::TreePop();
    return Changed;
}

bool FParticleEditorToolkit::DrawArray(void* Object, const CoreUObject::FProperty& Property) {
    const auto& Ops{*Property.GetArrayOps()};
    const auto* Struct{Ops.mElement->GetStruct()};

    if (Struct == nullptr || !ImGui::TreeNode(PropertyLabel(Property.GetName()).c_str())) {
        return false;
    }

    const auto Value{Property.ReadValue(Object)};
    const auto* Source{std::get<const void*>(Value)};
    const Uint32 Count{Ops.mSize(Source)};
    std::unique_ptr<void, void (*)(void*)> Draft{Ops.mCreate(), Ops.mDestroy};
    Ops.mResize(Draft.get(), Count);
    const auto Copy{[Struct](void* Destination, const void* Entry) {
        for (const auto* Field : Struct->GetProperties()) {
            Field->CopyValue(Destination, Entry);
        }
    }};

    bool Changed{};
    std::optional<Uint32> Removed{};

    for (Uint32 Index{}; Index < Count; ++Index) {
        ImGui::PushID(static_cast<int>(Index));
        auto* Entry{Ops.mAt(Draft.get(), Index)};
        Copy(Entry, Ops.mAt(const_cast<void*>(Source), Index));
        const Core::FString Label{"Entry " + std::to_string(Index + 1)};

        if (ImGui::TreeNode(Label.c_str())) {
            for (const auto* Field : Struct->GetProperties()) {
                if (Field->HasFlag(Core::EPropertyFlags::Edit) && !Field->HasFlag(Core::EPropertyFlags::ReadOnly)) {
                    ImGui::PushID(Field);
                    Changed |= DrawProperty(Entry, *Field);
                    ImGui::PopID();
                }
            }

            if (ImGui::SmallButton("Remove Entry")) {
                Removed = Index;
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (Removed) {
        for (Uint32 Index{*Removed}; Index + 1 < Count; ++Index) {
            Copy(Ops.mAt(Draft.get(), Index), Ops.mAt(Draft.get(), Index + 1));
        }
        Ops.mResize(Draft.get(), Count - 1);
        Changed = true;
    }

    if (ImGui::SmallButton("Add Entry")) {
        Ops.mResize(Draft.get(), Ops.mSize(Draft.get()) + 1);
        Changed = true;
    }

    ImGui::TreePop();
    return Changed && Property.WriteValue(Object, static_cast<const void*>(Draft.get()));
}

bool FParticleEditorToolkit::HasCurveTrack(const CoreUObject::UObject& Object) const {
    return std::ranges::any_of(Object.GetClass()->GetAllProperties(), [&Object](const auto* Property) {
        return IsCurveTrack(Object, *Property);
    });
}

void FParticleEditorToolkit::DrawCurve() {
    auto* Object{GetSelectedObject()};
    Core::TArray<const CoreUObject::FProperty*> Tracks{};

    if (Object != nullptr) {
        for (const auto* Property : Object->GetClass()->GetAllProperties()) {
            if (IsCurveTrack(*Object, *Property)) {
                Tracks.push_back(Property);
            }
        }
    }

    if (std::ranges::find(Tracks, mCurveProperty) == Tracks.end()) {
        ResetCurveSelection();
        mCurveProperty = Tracks.empty() ? nullptr : Tracks.front();
    }

    mCurveEditor.DrawFitButton(mCurveProperty != nullptr);

    const float Available{ImGui::GetContentRegionAvail().x};
    const float TrackWidth{std::min(ImGui::GetFontSize() * 12.0f, Available * 0.30f)};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(27, 34, 44, 255));
    if (ImGui::BeginChild("Curve Tracks", {std::max(TrackWidth, 1.0f), 0.0f}, ImGuiChildFlags_Borders)) {
        ImGui::SeparatorText("Tracks");
        if (Tracks.empty()) {
            ImGui::TextWrapped("Select a module and set its distribution to Curve.");
        }

        for (const auto* Track : Tracks) {
            ImGui::PushID(Track);
            if (EditorStyle::Selectable(PropertyLabel(Track->GetName()).c_str(), Track == mCurveProperty) && Track != mCurveProperty) {
                ResetCurveSelection();
                mCurveProperty = Track;
            }
            ImGui::PopID();
        }

        if (mCurveProperty != nullptr && mCurveProperty->GetStruct() == Engine::FVectorDistribution::StaticStruct()) {
            constexpr std::array AxisNames{"X", "Y", "Z"};
            ImGui::SeparatorText("Channels");
            for (Int32 Axis{}; Axis < static_cast<Int32>(AxisNames.size()); ++Axis) {
                ImGui::PushStyleColor(ImGuiCol_Text, CurveAxisColors[Axis]);
                if (EditorStyle::Selectable(AxisNames[Axis], mCurveAxis == Axis) && mCurveAxis != Axis) {
                    mCurveAxis = Axis;
                    mCurveEditor.Reset();
                }
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (ImGui::BeginChild("Curve Plot")) {
        if (mCurveProperty == nullptr) {
            mCurveEditor.Draw(nullptr, {});
        } else {
            ImGui::PushID(mCurveProperty);
            const auto Value{mCurveProperty->ReadValue(Object)};
            const auto* Address{std::get<const void*>(Value)};
            FCurveEditorWidget::FOptions Options{};
            Options.mEditable = mContext.CanEditWorld();
            Options.mMinimumTime = 0.0f;

            if (mCurveProperty->GetStruct() == Engine::FFloatDistribution::StaticStruct()) {
                const auto& Distribution{*static_cast<const Engine::FFloatDistribution*>(Address)};
                if (auto Keys{mCurveEditor.Draw(&Distribution.mCurveKeys, Options)}) {
                    auto Edited{Distribution};
                    Edited.mCurveKeys = std::move(*Keys);
                    if (mCurveProperty->WriteValue(Object, static_cast<const void*>(&Edited))) {
                        MarkDirty();
                    }
                }
            } else {
                const auto& Distribution{*static_cast<const Engine::FVectorDistribution*>(Address)};
                constexpr std::array Axes{&Engine::FVectorDistribution::mCurveKeysX, &Engine::FVectorDistribution::mCurveKeysY, &Engine::FVectorDistribution::mCurveKeysZ};
                const auto Channel{Axes[static_cast<Index_t>(mCurveAxis)]};
                Options.mColor = CurveAxisColors[mCurveAxis];
                ImGui::PushID(mCurveAxis);
                if (auto Keys{mCurveEditor.Draw(&(Distribution.*Channel), Options)}) {
                    auto Edited{Distribution};
                    Edited.*Channel = std::move(*Keys);
                    if (mCurveProperty->WriteValue(Object, static_cast<const void*>(&Edited))) {
                        MarkDirty();
                    }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}
