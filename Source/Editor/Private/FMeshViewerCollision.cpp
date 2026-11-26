/**
 * @file FMeshViewerCollision.cpp
 * @brief 정적 메시 뷰어에서 단순 충돌 형상(상자, 구, 캡슐)을 편집하는 칸을 구현합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Editor/FMeshViewer.h"
#include "Core/FilePath.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FRotator.h"
#include "Core/Math/FTransform.h"
#include "Core/Math/FVector.h"
#include "Asset/FAssetManager.h"
#include "Asset/UStaticMesh.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "PhysicsRuntime/FKAggregateGeom.h"
#include "PhysicsRuntime/UBodySetup.h"

#include "FPhysicsAssetDrawing.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace Editor {

    namespace {

        constexpr Uint32 ShapeNone{0};
        constexpr Uint32 ShapeSphere{1};
        constexpr Uint32 ShapeBox{2};
        constexpr Uint32 ShapeCapsule{3};

        /**
         * @brief 형상 크기의 하한이며 단위는 m입니다. 0이면 PhysX 형상을 만들 수 없습니다.
         */
        constexpr float MinimumSize{0.005f};

        /**
         * @brief 메시 경계 상자에 맞는 상자를 만듭니다.
         * @param[in] Bounds 메시의 로컬 경계 상자입니다.
         * @return 경계 상자와 같은 중심과 크기의 상자입니다.
         */
        PhysicsRuntime::FKBoxElem MakeBox(const FBox& Bounds) {
            const FVector Half{(Bounds.mMaximum - Bounds.mMinimum) * 0.5f};
            PhysicsRuntime::FKBoxElem Box{};

            Box.mCenter = (Bounds.mMinimum + Bounds.mMaximum) * 0.5f;
            Box.mHalfExtent = FVector{std::max(Half.x, MinimumSize), std::max(Half.y, MinimumSize), std::max(Half.z, MinimumSize)};

            return Box;
        }

        /**
         * @brief 메시 경계 상자를 감싸는 구를 만듭니다.
         * @param[in] Bounds 메시의 로컬 경계 상자입니다.
         * @return 경계 상자 중심에서 모서리까지 닿는 구입니다.
         */
        PhysicsRuntime::FKSphereElem MakeSphere(const FBox& Bounds) {
            const FVector Half{(Bounds.mMaximum - Bounds.mMinimum) * 0.5f};
            PhysicsRuntime::FKSphereElem Sphere{};

            Sphere.mCenter = (Bounds.mMinimum + Bounds.mMaximum) * 0.5f;
            Sphere.mRadius = Core::FLength::FromBase(std::max(Half.Length(), MinimumSize));

            return Sphere;
        }

        /**
         * @brief 메시 경계 상자를 Z축 방향으로 감싸는 캡슐을 만듭니다.
         * @param[in] Bounds 메시의 로컬 경계 상자입니다.
         * @return 반지름은 XY 중 큰 반쪽 크기(높이를 넘지 않음)이고, 원통 길이는 남는 높이입니다.
         */
        PhysicsRuntime::FKSphylElem MakeCapsule(const FBox& Bounds) {
            const FVector Half{(Bounds.mMaximum - Bounds.mMinimum) * 0.5f};
            const float Radius{std::max(std::min(std::max(Half.x, Half.y), Half.z), MinimumSize)};
            PhysicsRuntime::FKSphylElem Capsule{};

            Capsule.mCenter = (Bounds.mMinimum + Bounds.mMaximum) * 0.5f;
            Capsule.mRadius = Core::FLength::FromBase(Radius);
            Capsule.mLength = Core::FLength::FromBase(std::max(Half.z * 2.0f - Radius * 2.0f, 0.0f));

            return Capsule;
        }

        /**
         * @brief 중심과 회전 필드를 그리고 값이 바뀌면 반영합니다.
         * @param[in,out] Center 형상의 중심입니다.
         * @param[in,out] Rotation 형상의 회전(도)이며 회전을 쓰지 않는 형상이면 nullptr입니다.
         * @return 값이 바뀌었으면 true입니다.
         */
        bool DrawPlacementFields(FVector& Center, FRotator* Rotation) {
            bool Changed{};
            float Position[3]{Center.x, Center.y, Center.z};

            if (ImGui::DragFloat3("Center", Position, 0.005f, -1000.0f, 1000.0f, "%.3f")) {
                Center = FVector{Position[0], Position[1], Position[2]};
                Changed = true;
            }

            if (Rotation != nullptr) {
                float Angles[3]{Rotation->pitch, Rotation->yaw, Rotation->roll};

                if (ImGui::DragFloat3("Rotation (P/Y/R)", Angles, 0.5f, -360.0f, 360.0f, "%.1f")) {
                    *Rotation = FRotator{Angles[0], Angles[1], Angles[2]};
                    Changed = true;
                }
            }

            return Changed;
        }

        /**
         * @brief 길이 필드 하나를 그리고 값이 바뀌면 최솟값 이상으로 반영합니다.
         * @param[in] Label 필드 이름입니다.
         * @param[in,out] Value 길이(m)입니다.
         * @param[in] Min 최솟값(m)입니다.
         * @return 값이 바뀌었으면 true입니다.
         */
        bool DrawLengthField(const char* Label, Core::FLength& Value, float Min) {
            float Number{Value.GetBase()};

            if (!ImGui::DragFloat(Label, &Number, 0.005f, Min, 1000.0f, "%.3f")) {
                return false;
            }

            Value = Core::FLength::FromBase(std::max(Number, Min));

            return true;
        }

    }

    /**
     * @brief 충돌 형상을 메시 로컬 공간의 선으로 그립니다. 고른 형상의 중심에는 주황색 십자를 그립니다.
     */
    void Editor::FMeshViewer::BuildCollisionLines(Engine::FPrimitiveDrawInterface& Draw) const {
        const PhysicsRuntime::UBodySetup* Setup{mMesh != nullptr ? mMesh->GetBodySetup() : nullptr};

        if (Setup == nullptr) {
            return;
        }

        PhysicsAssetDrawing::DrawBodySetup(Draw, *Setup, FTransform{}, FVector{0.35f, 0.6f, 1.0f}, 1.5f);

        // 고른 형상의 중심에 십자 표시를 그린다. 같은 종류가 여러 개여도 어느 것을 편집 중인지 알 수 있다.
        const PhysicsRuntime::FKAggregateGeom& Shapes{mMesh->GetSimpleShapes()};
        FVector Center{};
        bool HasCenter{};

        if (mCollisionShapeType == ShapeSphere && mCollisionShapeIndex < Shapes.mSphereElems.size()) {
            Center = Shapes.mSphereElems[mCollisionShapeIndex].mCenter;
            HasCenter = true;
        } else if (mCollisionShapeType == ShapeBox && mCollisionShapeIndex < Shapes.mBoxElems.size()) {
            Center = Shapes.mBoxElems[mCollisionShapeIndex].mCenter;
            HasCenter = true;
        } else if (mCollisionShapeType == ShapeCapsule && mCollisionShapeIndex < Shapes.mSphylElems.size()) {
            Center = Shapes.mSphylElems[mCollisionShapeIndex].mCenter;
            HasCenter = true;
        }

        if (HasCenter) {
            const FBox& Bounds{mMesh->GetBounds()};
            const float Arm{std::max((Bounds.mMaximum - Bounds.mMinimum).Length() * 0.03f, 0.01f)};
            const FVector Orange{1.0f, 0.65f, 0.15f};

            Draw.DrawLine(Center - FVector{Arm, 0.0f, 0.0f}, Center + FVector{Arm, 0.0f, 0.0f}, Orange, false, 2.5f);
            Draw.DrawLine(Center - FVector{0.0f, Arm, 0.0f}, Center + FVector{0.0f, Arm, 0.0f}, Orange, false, 2.5f);
            Draw.DrawLine(Center - FVector{0.0f, 0.0f, Arm}, Center + FVector{0.0f, 0.0f, Arm}, Orange, false, 2.5f);
        }
    }

    /**
     * @brief 정적 메시의 단순 충돌 형상 칸을 그립니다.
     */
    void Editor::FMeshViewer::DrawCollision() {
        if (mMesh == nullptr || !ImGui::CollapsingHeader("Collision")) {
            return;
        }

        PhysicsRuntime::FKAggregateGeom Shapes{mMesh->GetSimpleShapes()};
        const FBox Bounds{mMesh->GetBounds()};
        bool Changed{};

        if (!Shapes.IsEmpty()) {
            ImGui::TextUnformatted("Source: Simple shapes (edited)");
        } else if (!mMesh->GetConvexElems().empty()) {
            ImGui::Text("Source: Imported convex (%zu pieces). Adding a shape replaces it.", mMesh->GetConvexElems().size());
        } else {
            ImGui::TextUnformatted("Source: Mesh bounds box (default)");
        }

        ImGui::Checkbox("Show Collision", &mShowCollision);

        // 형상 추가: 새 형상은 메시 경계에 맞춰 만든다.
        ImGui::TextUnformatted("Add");
        ImGui::SameLine();

        if (ImGui::SmallButton("Box")) {
            Shapes.mBoxElems.push_back(MakeBox(Bounds));
            mCollisionShapeType = ShapeBox;
            mCollisionShapeIndex = Shapes.mBoxElems.size() - 1;
            Changed = true;
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Sphere")) {
            Shapes.mSphereElems.push_back(MakeSphere(Bounds));
            mCollisionShapeType = ShapeSphere;
            mCollisionShapeIndex = Shapes.mSphereElems.size() - 1;
            Changed = true;
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Capsule")) {
            Shapes.mSphylElems.push_back(MakeCapsule(Bounds));
            mCollisionShapeType = ShapeCapsule;
            mCollisionShapeIndex = Shapes.mSphylElems.size() - 1;
            Changed = true;
        }

        // 메시 경계에 맞추기: 기존 형상을 모두 지우고 하나로 바꾼다.
        ImGui::TextUnformatted("Fit");
        ImGui::SameLine();

        const auto Fit{[&](Uint32 Type) {
            Shapes = PhysicsRuntime::FKAggregateGeom{};

            if (Type == ShapeBox) {
                Shapes.mBoxElems.push_back(MakeBox(Bounds));
            } else if (Type == ShapeSphere) {
                Shapes.mSphereElems.push_back(MakeSphere(Bounds));
            } else {
                Shapes.mSphylElems.push_back(MakeCapsule(Bounds));
            }

            mCollisionShapeType = Type;
            mCollisionShapeIndex = 0;
            Changed = true;
        }};

        if (ImGui::SmallButton("Box##Fit")) {
            Fit(ShapeBox);
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Sphere##Fit")) {
            Fit(ShapeSphere);
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Capsule##Fit")) {
            Fit(ShapeCapsule);
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Clear")) {
            Shapes = PhysicsRuntime::FKAggregateGeom{};
            mCollisionShapeType = ShapeNone;
            mCollisionShapeIndex = 0;
            Changed = true;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove all simple shapes and use the imported convex or the bounds box again.");
        }

        // 형상 목록: 종류별로 번호를 붙여 한 줄씩 보여 주고, 누르면 아래에서 편집한다.
        const auto Row{[&](const char* Label, Uint32 Type, Index_t Count) {
            for (Index_t Index{}; Index < Count; ++Index) {
                const Core::FString Text{Core::FString{Label} + " " + std::to_string(Index) + "##Shape" + std::to_string(Type) + "_" + std::to_string(Index)};

                if (ImGui::Selectable(Text.c_str(), mCollisionShapeType == Type && mCollisionShapeIndex == Index)) {
                    mCollisionShapeType = Type;
                    mCollisionShapeIndex = Index;
                }
            }
        }};

        Row("Box", ShapeBox, Shapes.mBoxElems.size());
        Row("Sphere", ShapeSphere, Shapes.mSphereElems.size());
        Row("Capsule", ShapeCapsule, Shapes.mSphylElems.size());

        // 고른 형상이 없어졌으면 선택을 푼다.
        const bool SelectionValid{(mCollisionShapeType == ShapeBox && mCollisionShapeIndex < Shapes.mBoxElems.size()) || (mCollisionShapeType == ShapeSphere && mCollisionShapeIndex < Shapes.mSphereElems.size()) || (mCollisionShapeType == ShapeCapsule && mCollisionShapeIndex < Shapes.mSphylElems.size())};

        if (!SelectionValid) {
            mCollisionShapeType = ShapeNone;
        }

        if (mCollisionShapeType != ShapeNone) {
            ImGui::Separator();

            bool Delete{};

            if (mCollisionShapeType == ShapeBox) {
                PhysicsRuntime::FKBoxElem& Box{Shapes.mBoxElems[mCollisionShapeIndex]};

                Changed |= DrawPlacementFields(Box.mCenter, &Box.mRotation);

                float Half[3]{Box.mHalfExtent.x, Box.mHalfExtent.y, Box.mHalfExtent.z};

                if (ImGui::DragFloat3("Half Extent", Half, 0.005f, MinimumSize, 1000.0f, "%.3f")) {
                    Box.mHalfExtent = FVector{std::max(Half[0], MinimumSize), std::max(Half[1], MinimumSize), std::max(Half[2], MinimumSize)};
                    Changed = true;
                }
            } else if (mCollisionShapeType == ShapeSphere) {
                PhysicsRuntime::FKSphereElem& Sphere{Shapes.mSphereElems[mCollisionShapeIndex]};

                Changed |= DrawPlacementFields(Sphere.mCenter, nullptr);
                Changed |= DrawLengthField("Radius", Sphere.mRadius, MinimumSize);
            } else {
                PhysicsRuntime::FKSphylElem& Capsule{Shapes.mSphylElems[mCollisionShapeIndex]};

                Changed |= DrawPlacementFields(Capsule.mCenter, &Capsule.mRotation);
                Changed |= DrawLengthField("Radius", Capsule.mRadius, MinimumSize);
                Changed |= DrawLengthField("Length", Capsule.mLength, 0.0f);
            }

            if (ImGui::Button("Delete Shape")) {
                Delete = true;
            }

            if (Delete) {
                if (mCollisionShapeType == ShapeBox) {
                    Shapes.mBoxElems.erase(Shapes.mBoxElems.begin() + static_cast<std::ptrdiff_t>(mCollisionShapeIndex));
                } else if (mCollisionShapeType == ShapeSphere) {
                    Shapes.mSphereElems.erase(Shapes.mSphereElems.begin() + static_cast<std::ptrdiff_t>(mCollisionShapeIndex));
                } else {
                    Shapes.mSphylElems.erase(Shapes.mSphylElems.begin() + static_cast<std::ptrdiff_t>(mCollisionShapeIndex));
                }

                mCollisionShapeType = ShapeNone;
                Changed = true;
            }
        }

        // 바뀐 형상은 검사를 통과하면 메시에 반영하고, 메시의 바디 셋업이 다시 만들어진다. 이미 열린 PIE에는 다음 실행부터 적용된다.
        if (Changed) {
            if (mMesh->SetSimpleShapes(std::move(Shapes))) {
                mCollisionUnsaved = true;
                mCollisionMessage.clear();
            } else {
                mCollisionMessage = "Invalid shape value (not finite or too small).";
            }
        }

        ImGui::BeginDisabled(!mCollisionUnsaved);

        if (ImGui::Button(mCollisionUnsaved ? "Save Collision *" : "Save Collision")) {
            Core::FString Error{};

            if (mAssets != nullptr && mAssets->SaveMesh(Core::FilePath::FromUtf8(mPath), *mMesh, Error)) {
                mCollisionUnsaved = false;
                mCollisionMessage = "Saved the collision into " + mPath;
            } else {
                mCollisionMessage = Error.empty() ? "Cannot save the mesh." : Error;
            }
        }

        ImGui::EndDisabled();

        if (!mCollisionMessage.empty()) {
            ImGui::TextWrapped("%s", mCollisionMessage.c_str());
        }
    }

}
