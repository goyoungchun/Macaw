/**
 * @file FAnimGraphWidget.cpp
 * @brief 애니메이션 인스턴스의 노드 그래프를 언리얼 AnimGraph처럼 오른쪽의 Output Pose로 모이는 노드와 링크로 그립니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Core/Literals/UnitLiterals.h"
#include "Editor/Widget/FAnimGraphWidget.h"
#include "Editor/FEditorContext.h"
#include "Engine/AActor.h"
#include "Asset/Animation/UBlendSpace.h"
#include "Asset/Animation/UAnimSequence.h"
#include "Engine/Animation/FAnimNode_Base.h"
#include "Engine/Animation/FAnimNode_BlendSpacePlayer.h"
#include "Engine/Animation/FAnimValueNode.h"
#include "Engine/Animation/UAnimGraphInstance.h"
#include "Engine/Animation/UAnimInstance.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/FProperty.h"
#include "CoreUObject/Reflection/UClass.h"
#include "TGL/GraphEditor.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <unordered_set>

using namespace Core::Literals;

namespace Editor {

    /**
     * @brief 애니메이션 인스턴스의 노드 그래프를 언리얼 AnimGraph처럼 오른쪽의 Output Pose로 모이는 노드와 링크로 그립니다.
     */
    namespace {
        // Output Pose 노드, 그 입력 핀, 루트에서 들어오는 링크는 다른 식별자와 겹치지 않는 1을 씁니다(노드·핀·링크는 서로 다른 식별자 공간입니다).

        /**
         * @brief 최종 포즈 출력 노드의 예약 키입니다.
         */
        constexpr Uint64 OutputKey{1};

        /**
         * @brief 출력 핀을 입력 핀·노드 키와 구분하는 비트입니다.
         */
        constexpr Uint64 OutputPinBits{0xFF};

        /**
         * @brief Float 값 입력 핀을 다른 키와 구분하는 비트입니다.
         */
        constexpr Uint64 FloatInputBit{Uint64{1} << 60};

        /**
         * @brief 변수 Getter 노드를 다른 키와 구분하는 비트입니다.
         */
        constexpr Uint64 GetterBit{Uint64{1} << 61};

        /**
         * @brief 저장된 Getter ID를 임시 실행 키와 구분하는 비트입니다.
         */
        constexpr Uint64 SavedGetterBit{Uint64{1} << 59};

        /**
         * @brief 실행 그래프 열 사이 간격(픽셀)입니다.
         */
        constexpr float ColumnSpacing{340.0f};

        /**
         * @brief 실행 그래프 행 사이 간격(픽셀)입니다.
         */
        constexpr float RowSpacing{260.0f};

        /**
         * @brief 현재 프레임에서 편집 가능한 파라미터의 변경을 실행 인스턴스에 적용합니다.
         * @details 파라미터 포인터는 DrawGraph의 해당 프레임에만 등록합니다. 적용 시 등록한 속성과 값 타입을 확인합니다.
         */
        class FLiveAnimGraphEdits final : public TGL::FGraphSchema {
        public:
            explicit FLiveAnimGraphEdits(const TGL::IGraphDocument& Document);

        public:
            /**
             * @brief 노드의 Float 속성과 현재 인스턴스의 값 및 범위를 연결합니다.
             * @param Node 속성을 소유하는 노드입니다.
             * @param Key 문서의 속성 키입니다.
             * @param Value 실행 인스턴스의 값 참조와 허용 범위입니다.
             */
            void Bind(TGL::FNodeId Node, const std::string& Key, const Engine::FAnimNodeFloatValue& Value);
            /**
             * @brief 노드의 Bool 속성과 현재 인스턴스의 값을 연결합니다.
             * @param Node 속성을 소유하는 노드입니다.
             * @param Key 문서의 속성 키입니다.
             * @param Value 이 프레임에서 접근할 수 있는 Bool 값의 주소입니다.
             */
            void Bind(TGL::FNodeId Node, const std::string& Key, bool* Value);
        private:
            TGL::FEditResult ApplyEdit(const TGL::FSetProperty& Edit) override;

        private:
            std::map<std::pair<TGL::FNodeId, std::string>, std::variant<Engine::FAnimNodeFloatValue, bool*>> mValues{}; ///< 노드와 속성 키별 실행 값과 범위입니다.
            bool mApplied{}; ///< 이 프레임에서 값이 이미 바뀌었는지 나타냅니다.
        };

        FLiveAnimGraphEdits::FLiveAnimGraphEdits(const TGL::IGraphDocument& Document)
            : TGL::FGraphSchema{Document, false} {
        }

        void FLiveAnimGraphEdits::Bind(TGL::FNodeId Node, const std::string& Key, const Engine::FAnimNodeFloatValue& Value) {
            mValues.emplace(std::pair{Node, Key}, Value);
        }

        void FLiveAnimGraphEdits::Bind(TGL::FNodeId Node, const std::string& Key, bool* Value) {
            mValues.emplace(std::pair{Node, Key}, Value);
        }

        TGL::FEditResult FLiveAnimGraphEdits::ApplyEdit(const TGL::FSetProperty& Edit) {
            using TGL::EEditStatus;

            if (mApplied) {
                return {EEditStatus::Rejected};
            }

            const auto* Node{std::get_if<TGL::FNodeId>(&Edit.mItem)};

            if (Node == nullptr) {
                return {EEditStatus::Rejected};
            }

            const auto Found{mValues.find({*Node, Edit.mKey})};

            if (Found == mValues.end()) {
                return {EEditStatus::Rejected};
            }

            if (const auto* Float{std::get_if<Engine::FAnimNodeFloatValue>(&Found->second)}) {
                const auto* Number{std::get_if<double>(&Edit.mValue)};

                if (Number == nullptr) {
                    return {EEditStatus::Rejected};
                }

                const double Value{Float->mMin < Float->mMax ? std::clamp(*Number, static_cast<double>(Float->mMin), static_cast<double>(Float->mMax)) : *Number};
                const float Next{static_cast<float>(Value)};

                if (!std::isfinite(Next)) {
                    return {EEditStatus::Rejected};
                }

                mApplied = std::exchange(*Float->mValue, Next) != Next;
            } else {
                const auto* Flag{std::get_if<bool>(&Edit.mValue)};

                if (Flag == nullptr) {
                    return {EEditStatus::Rejected};
                }

                mApplied = std::exchange(*std::get<bool*>(Found->second), *Flag) != *Flag;
            }

            return {mApplied ? EEditStatus::Applied : EEditStatus::NoChange};
        }

        // 플레이할 때마다 노드 객체가 새로 만들어져도 같은 노드에 같은 값이 나오는 식별자입니다. 아래 8비트는 핀 번호로 씁니다.
        // 그래프 에셋 노드는 에셋 안의 식별자로, C++ 인스턴스의 멤버 노드는 인스턴스 안의 오프셋과 클래스 이름으로 만듭니다.

        /**
         * @brief 원본 또는 실행 노드를 편집기에서 식별할 키로 변환합니다.
         * @param Instance 노드의 원본 대응과 식별 키를 제공하는 인스턴스입니다.
         * @param Node 조회·연결·갱신할 노드입니다.
         * @return 노드를 식별하는 편집기 키입니다.
         */
        Uint64 NodeKey(const Engine::UAnimInstance& Instance, const Engine::FAnimNode_Base* Node) {
            if (const auto* Graph{CoreUObject::Cast<Engine::UAnimGraphInstance>(&Instance)}) {
                if (const Uint64 Key{Graph->GetNodeKey(Node)}; Key != 0) {
                    return (Key << 8) | (Uint64{1} << 56);
                }
            }

            const auto Base{reinterpret_cast<std::uintptr_t>(&Instance)};
            const auto Address{reinterpret_cast<std::uintptr_t>(Node)};

            if (Address >= Base && Address - Base < (std::uintptr_t{1} << 24)) {
                const Uint64 ClassBits{static_cast<Uint64>(std::hash<Core::FString>{}(Instance.GetClass()->GetName()) & 0xFFFF) << 34};

                return ((static_cast<Uint64>(Address - Base) + 1) << 8) | ClassBits | (Uint64{1} << 57);
            }

            // 인스턴스 밖에 있는 노드는 안정 식별자를 만들 수 없어 주소를 씁니다.
            return (static_cast<Uint64>(Address) << 8) | (Uint64{1} << 62);
        }

        /**
         * @brief 노드 키와 입력 번호를 편집기 입력 핀 키로 인코딩합니다.
         * @param Node 키로 변환하거나 조회할 노드 ID입니다.
         * @param Index 대상 배열의 항목 인덱스입니다.
         * @return 입력 핀을 식별하는 키입니다.
         */
        Uint64 InputPinKey(Uint64 Node, Index_t Index) {
            return Node | static_cast<Uint64>(Index + 1);
        }

        /**
         * @brief 노드 키를 편집기 출력 핀 키로 인코딩합니다.
         * @param Node 키로 변환하거나 조회할 노드 ID입니다.
         * @return 출력 핀을 식별하는 키입니다.
         */
        Uint64 OutputPinKey(Uint64 Node) {
            return Node | OutputPinBits;
        }

        /**
         * @brief 두 UI 색상을 가중치로 선형 보간합니다.
         * @param From 연결을 시작할 원본 항목 ID입니다.
         * @param To 연결할 대상 항목 ID입니다.
         * @param Alpha 0~1의 보간 가중치입니다.
         * @return 보간한 RGBA 색상입니다.
         */
        ImVec4 Mix(const ImVec4& From, const ImVec4& To, float Alpha) {
            return {From.x + (To.x - From.x) * Alpha, From.y + (To.y - From.y) * Alpha, From.z + (To.z - From.z) * Alpha, From.w + (To.w - From.w) * Alpha};
        }

        /**
         * @brief 포즈 연결을 순회하여 중복 없는 실행 노드 목록을 수집합니다.
         * @param Node 조회·연결·갱신할 노드입니다.
         * @param Nodes 발견한 비소유 실행 노드를 추가할 배열입니다.
         */
        void CollectNodes(Engine::FAnimNode_Base* Node, Core::TArray<Engine::FAnimNode_Base*>& Nodes) {
            if (Node == nullptr || std::find(Nodes.begin(), Nodes.end(), Node) != Nodes.end()) {
                return;
            }

            Nodes.push_back(Node);

            Core::TArray<Engine::FAnimNodePoseInput> Inputs{};

            Node->GetPoseInputs(Inputs);

            for (const Engine::FAnimNodePoseInput& Input : Inputs) {
                CollectNodes(Input.mLink->mLinkedNode, Nodes);
            }
        }

        /**
         * @brief 포즈 연결을 따라 노드별 표시 열 깊이를 계산합니다.
         * @param Node 조회·연결·갱신할 노드입니다.
         * @param Depth 탐색할 상위 보기의 깊이 또는 노드 배치 깊이입니다.
         * @param Depths 노드별 표시 깊이를 기록할 맵입니다.
         * @param Guard 잘못된 연결 순회에 대한 깊이 제한입니다.
         */
        void AssignDepth(Engine::FAnimNode_Base* Node, Int32 Depth, Core::TMap<Engine::FAnimNode_Base*, Int32>& Depths, Int32 Guard) {
            // 순환 연결이 있어도 멈추도록 깊이를 노드 수로 제한합니다.
            if (Node == nullptr || Guard < 0) {
                return;
            }

            auto& Current{Depths[Node]};

            Current = std::max(Current, Depth);

            Core::TArray<Engine::FAnimNodePoseInput> Inputs{};

            Node->GetPoseInputs(Inputs);

            for (const Engine::FAnimNodePoseInput& Input : Inputs) {
                AssignDepth(Input.mLink->mLinkedNode, Depth + 1, Depths, Guard - 1);
            }
        }
    }

    /**
     * @brief 인스턴스에서 읽은 문서와 프레임 사이에 유지할 캔버스 상태입니다.
     */
    struct FAnimGraphWidget::FGraphState {
        TGL::FGraphSnapshot mDocument{}; ///< 현재 인스턴스의 노드와 링크를 표시하는 자료 스냅샷입니다.
        TGL::FGraphEditor mEditor{}; ///< 캔버스와 사용자 선택을 관리하는 공용 편집기입니다.
        std::unordered_map<Uint64, TGL::FPoint> mPositions{}; ///< 현재 실행 그래프가 제공한 위치 또는 자동 배치 위치입니다.
        Core::FString mSource{}; ///< 현재 그래프를 식별하는 에셋 경로 또는 인스턴스 클래스 이름입니다.
        Core::FRevision mSourceRevision{}; ///< 위치 캐시를 구성할 때 읽은 실행 그래프 원본 리비전입니다.
    };

    FAnimGraphWidget::FAnimGraphWidget(Editor::FEditorContext* Context)
        : Editor::FEditorContextWidget{Context, "AnimGraph"},
          mGraph{std::make_unique<FGraphState>()} {
    }

    FAnimGraphWidget::~FAnimGraphWidget() = default;

    void FAnimGraphWidget::RequestLayout() {
        mLayoutRequested = true;
    }

    Engine::USkeletalMeshComponent* FAnimGraphWidget::FindTarget() const {
        const Editor::FEditorContext* Context{GetEditorContext()};

        if (Context == nullptr) {
            return nullptr;
        }

        if (auto* Component{CoreUObject::Cast<Engine::USkeletalMeshComponent>(Context->GetSelectedObject())}) {
            return Component;
        }

        if (const Engine::AActor* Actor{Context->GetSelectedActor()}) {
            for (const auto& Component : Actor->GetComponents()) {
                if (auto* SkeletalMesh{CoreUObject::Cast<Engine::USkeletalMeshComponent>(Component.get())}) {
                    return SkeletalMesh;
                }
            }
        }

        return nullptr;
    }

    void FAnimGraphWidget::DrawContent(float) {
        Engine::USkeletalMeshComponent* Component{FindTarget()};
        Engine::UAnimInstance* Instance{Component != nullptr ? Component->GetAnimInstance() : nullptr};

        if (Component == nullptr) {
            ImGui::TextDisabled("스켈레탈 메시 컴포넌트가 있는 액터를 선택하세요.");

            return;
        }

        if (Instance == nullptr) {
            ImGui::TextDisabled("%s에 애니메이션 인스턴스가 없습니다.", Component->GetName().c_str());
            ImGui::TextWrapped("Details 패널에서 AnimClass를 지정하면 그 그래프를 볼 수 있습니다. AnimClass가 비어 있으면 플레이 중에만 단일 노드 인스턴스가 생깁니다.");

            return;
        }

        const bool Playing{Component->HasBegunPlay()};

        ImGui::Text("%s", Component->GetOwner() != nullptr ? Component->GetOwner()->GetName().c_str() : Component->GetName().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s · %s", Instance->GetClass()->GetName().c_str(), Playing ? "플레이 중" : "편집 모드 (플레이하면 가중치와 재생 상태가 보입니다)");
        ImGui::SameLine();

        if (ImGui::SmallButton("다시 배치")) {
            mLayoutRequested = true;
        }

        if (ImGui::BeginTable("AnimGraphLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) {
            ImGui::TableSetupColumn("Variables", ImGuiTableColumnFlags_WidthFixed, mVariablesWidth);
            ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            if (ImGui::BeginChild("AnimVariables")) {
                DrawVariables(*Instance, Playing);
            }

            ImGui::EndChild();
            ImGui::TableSetColumnIndex(1);
            DrawGraph(*Instance, Playing);
            ImGui::EndTable();
        }
    }

    void FAnimGraphWidget::DrawVariables(Engine::UAnimInstance& Instance, bool Playing) {
        ImGui::SeparatorText("Variables");

        bool HasVariables{};

        for (const CoreUObject::UClass* Class{Instance.GetClass()}; Class != nullptr && Class->IsChildOf(Engine::UAnimInstance::StaticClass()); Class = Class->GetSuperClass()) {
            for (const CoreUObject::FProperty* Property : Class->GetProperties()) {
                if (!Property->HasFlag(Core::EPropertyFlags::Edit)) {
                    continue;
                }

                HasVariables = true;

                CoreUObject::FPropertyValue Value{Property->ReadValue(&Instance)};
                const bool Editable{Playing && !Property->HasFlag(Core::EPropertyFlags::ReadOnly)};
                const char* Name{Property->GetName().c_str()};

                ImGui::PushID(Property);

                // 플레이 중에는 저장되지 않는 복제 월드이므로 값을 바로 씁니다. 편집 모드의 설정은 실행 취소가 되도록 Details 패널에서 바꿉니다.
                if (auto* Float{std::get_if<float>(&Value)}) {
                    ImGui::SetNextItemWidth(-1.0f);

                    if (Editable) {
                        if (ImGui::DragFloat("##Value", Float, 0.01f, 0.0f, 0.0f, (Core::FString{Name} + ": %.3f").c_str())) {
                            Property->WriteValue(&Instance, Value);
                        }
                    } else {
                        ImGui::Text("%s: %.3f", Name, *Float);
                    }
                } else if (auto* Bool{std::get_if<bool>(&Value)}) {
                    ImGui::BeginDisabled(!Editable);

                    if (ImGui::Checkbox(Name, Bool) && Editable) {
                        Property->WriteValue(&Instance, Value);
                    }

                    ImGui::EndDisabled();
                } else if (auto* String{std::get_if<Core::FString>(&Value)}) {
                    ImGui::TextUnformatted(Name);
                    ImGui::TextDisabled("  %s", String->empty() ? "(없음)" : String->c_str());
                } else {
                    ImGui::TextDisabled("%s", Name);
                }

                ImGui::PopID();
            }
        }

        // 그래프 에셋 변수처럼 리플렉션 프로퍼티가 아닌 실행 중 변수입니다.
        const auto* Graph{CoreUObject::Cast<Engine::UAnimGraphInstance>(&Instance)};
        Core::TArray<Engine::FAnimNodeValue> RuntimeVariables{};

        Instance.GetDebugVariables(RuntimeVariables);

        if (!RuntimeVariables.empty()) {
            ImGui::SeparatorText("Graph Variables");
        }

        for (const Engine::FAnimNodeValue& Variable : RuntimeVariables) {
            HasVariables = true;

            ImGui::PushID(Variable.mName.c_str());

            if (const Engine::FAnimNodeFloatValue* Float{std::get_if<Engine::FAnimNodeFloatValue>(&Variable.mValue)}; Float != nullptr && Float->mValue != nullptr) {
                const bool OwnerSpeed{Graph != nullptr && Graph->GetVariableSource(Variable.mName) == Asset::EAnimGraphVariableSource::OwnerSpeed};

                if (Playing && !OwnerSpeed) {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::DragFloat("##Value", Float->mValue, 0.01f, 0.0f, 0.0f, (Variable.mName + ": %.3f").c_str());
                } else {
                    ImGui::Text("%s: %.3f", Variable.mName.c_str(), *Float->mValue);

                    if (OwnerSpeed) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("<- Owner Speed");
                    }
                }
            }

            if (bool* const* Bool{std::get_if<bool*>(&Variable.mValue)}; Bool != nullptr && *Bool != nullptr) {
                ImGui::BeginDisabled(!Playing);
                ImGui::Checkbox(Variable.mName.c_str(), *Bool);
                ImGui::EndDisabled();
            }

            ImGui::PopID();
        }

        if (!HasVariables) {
            ImGui::TextDisabled("편집할 변수가 없습니다.");
        }

        if (Playing) {
            const FVector& Velocity{Instance.GetOwnerVelocity()};

            ImGui::SeparatorText("Owner");
            ImGui::Text("Velocity: %.2f, %.2f, %.2f", Velocity.x, Velocity.y, Velocity.z);
        } else {
            ImGui::Spacing();
            ImGui::TextWrapped("설정은 Details 패널의 Anim Instance 항목에서 편집합니다.");
        }
    }

    bool FAnimGraphWidget::PlaceNodes(const Engine::UAnimInstance& Instance, Engine::FAnimNode_Base* Root, bool All) {
        Core::TArray<Engine::FAnimNode_Base*> Nodes{};

        CollectNodes(Root, Nodes);

        if (All) {
            mGraph->mPositions.clear();
        }

        const bool Missing{!mGraph->mPositions.contains(OutputKey) || std::any_of(Nodes.begin(), Nodes.end(), [this, &Instance](const Engine::FAnimNode_Base* Node) {
            return !mGraph->mPositions.contains(NodeKey(Instance, Node));
        })};

        if (!Missing) {
            return false;
        }

        // 자동 배치 위치입니다. 언리얼 AnimGraph처럼 Output Pose를 오른쪽 끝에 두고, 입력 쪽 노드일수록 왼쪽 열에 놓습니다.
        Core::TMap<Engine::FAnimNode_Base*, Int32> Depths{};
        Core::TMap<Int32, Core::TArray<Engine::FAnimNode_Base*>> Columns{};

        AssignDepth(Root, 1, Depths, static_cast<Int32>(Nodes.size()));

        for (Engine::FAnimNode_Base* Node : Nodes) {
            Columns[Depths[Node]].push_back(Node);
        }

        const auto Place{[this](Uint64 Key, float X, float Y) {
            mGraph->mPositions.try_emplace(Key, TGL::FPoint{X, Y});
        }};
        float X{};
        float Y{};

        // 그래프 에셋으로 만든 인스턴스는 에디터에서 놓은 위치를 씁니다.
        if (!mGraph->mPositions.contains(OutputKey)) {
            if (Instance.GetNodeLayout(nullptr, X, Y)) {
                Place(OutputKey, X, Y);
            } else {
                Place(OutputKey, 0.0f, 0.0f);
            }
        }

        for (const auto& [Depth, Column] : Columns) {
            const float Top{-0.5f * static_cast<float>(Column.size() - 1) * RowSpacing};

            for (Index_t Row{}; Row < Column.size(); ++Row) {
                const Uint64 Key{NodeKey(Instance, Column[Row])};

                if (mGraph->mPositions.contains(Key)) {
                    continue;
                }

                if (Instance.GetNodeLayout(Column[Row], X, Y)) {
                    Place(Key, X, Y);
                } else {
                    Place(Key, -static_cast<float>(Depth) * ColumnSpacing, Top + static_cast<float>(Row) * RowSpacing);
                }
            }
        }

        return true;
    }

    void FAnimGraphWidget::DrawGraph(Engine::UAnimInstance& Instance, bool Playing) {
        Engine::FAnimNode_Base* Root{Instance.GetRootNode()};
        const auto* Graph{CoreUObject::Cast<Engine::UAnimGraphInstance>(&Instance)};
        const Uint64 GraphKey{Graph != nullptr ? (static_cast<Uint64>(std::hash<Core::FString>{}(Graph->GetAnimGraph())) & 0xFFFFF) << 39 : 0};
        Core::TMap<const Engine::FAnimValueNode_Base*, Uint64> ValueNodeKeys{};

        Core::TMap<const Engine::FAnimNode_Base*, float> NodeWeights{};
        Core::TMap<const Engine::FPoseLink*, float> LinkWeights{};
        Core::TMap<const Engine::FAnimNode_Base*, Core::TArray<Asset::FBlendSampleWeight>> SampleWeights{};

        if (Playing) {
            const auto& Visits{Instance.GetDebugVisits()};
            Core::TArray<float> Weights{};
            Core::TArray<float> SampleTotals{};

            Weights.resize(Visits.size());
            SampleTotals.resize(Visits.size());

            for (const Engine::FAnimNodeVisit& Visit : Visits) {
                if (Visit.mSampleIndex != InvalidIndex && Visit.mParentIndex >= 0 && static_cast<Index_t>(Visit.mParentIndex) < Visits.size()) {
                    SampleTotals[static_cast<Index_t>(Visit.mParentIndex)] += Visit.mWeight;
                }
            }

            for (Index_t Index{}; Index < Visits.size(); ++Index) {
                const Engine::FAnimNodeVisit& Visit{Visits[Index]};
                float Weight{Visit.mWeight};

                if (Visit.mParentIndex >= 0 && static_cast<Index_t>(Visit.mParentIndex) < Index) {
                    const Index_t Parent{static_cast<Index_t>(Visit.mParentIndex)};
                    Weight *= Weights[Parent];

                    if (Visit.mSampleIndex != InvalidIndex) {
                        Weight = SampleTotals[Parent] > 0.0f ? Weight / SampleTotals[Parent] : 0.0f;
                    }
                }

                Weights[Index] = Weight;

                if (Visit.mSampleIndex != InvalidIndex) {
                    auto& Samples{SampleWeights[Visit.mNode]};
                    const auto Existing{std::find_if(Samples.begin(), Samples.end(), [&Visit](const Asset::FBlendSampleWeight& Sample) {
                        return Sample.mSampleIndex == Visit.mSampleIndex;
                    })};

                    if (Existing != Samples.end()) {
                        Existing->mWeight += Weight;
                    } else {
                        Samples.push_back({Visit.mSampleIndex, Weight});
                    }

                    continue;
                }

                NodeWeights[Visit.mNode] += Weight;

                if (Visit.mLink != nullptr) {
                    LinkWeights[Visit.mLink] += Weight;
                }
            }
        }

        const auto WeightOf{[](const auto& Weights, const auto* Item) {
            const auto Weight{Weights.find(Item)};
            return Weight != Weights.end() ? std::clamp(Weight->second, 0.0f, 1.0f) : 0.0f;
        }};

        const Core::FString Source{Graph != nullptr ? "Asset:" + Graph->GetAnimGraph() : "Class:" + Instance.GetClass()->GetName()};
        const Core::FRevision Revision{Graph != nullptr ? Graph->GetCompiledGraphRevision() : Core::FRevision{}};
        const bool ChangedSource{mGraph->mSource != Source || mGraph->mSourceRevision != Revision};
        const bool ResetLayout{mLayoutRequested || ChangedSource};
        mGraph->mSource = Source;
        mGraph->mSourceRevision = Revision;

        if (ChangedSource) {
            mGraph->mEditor.SetSelection({});
        }

        // 처음 보는 노드만 배치하므로, 플레이를 다시 시작해도 사용자가 옮긴 위치가 유지됩니다.
        bool Placed{PlaceNodes(Instance, Root, ResetLayout)};
        mLayoutRequested = false;
        Core::TArray<Engine::FAnimNode_Base*> Nodes{};
        CollectNodes(Root, Nodes);

        std::vector<TGL::FNodeView> NodeViews{};
        std::vector<TGL::FLinkView> LinkViews{};
        std::vector<TGL::FNodeId> ActiveNodes{};
        std::vector<TGL::FLinkId> ActiveLinks{};
        FLiveAnimGraphEdits LiveEdits{mGraph->mDocument};
        const ImVec4 PoseColor{TGL::Canvas::ConnectionColor(TGL::Canvas::EConnectionRole::Pose)};
        const ImVec4 FloatColor{TGL::Canvas::ConnectionColor(TGL::Canvas::EConnectionRole::Number)};
        const TGL::Canvas::Style Style{};
        const ImVec4 ActiveColor{Style.mActiveColor};
        const ImU32 PoseTint{ImGui::ColorConvertFloat4ToU32(PoseColor)};
        const ImU32 FloatTint{ImGui::ColorConvertFloat4ToU32(FloatColor)};

        // 링크는 입력 노드의 출력 핀에서 받는 노드의 입력 핀으로 이어지며, 이번 갱신에서 입력 노드가 기여한 가중치만큼 굵고 밝게 그립니다.
        // 활성 링크에는 흐름 표시를 띄워 포즈가 어느 쪽으로 흐르는지 보여 줍니다.
        // 흐름 표시는 한 번 띄우면 잠시 이어지므로 1.5초마다 다시 띄웁니다.
        const bool Pulse{Playing && std::fmod(ImGui::GetTime(), static_cast<double>((1.5_s).GetBase())) < static_cast<double>(ImGui::GetIO().DeltaTime)};
        const auto AddPoseLink{[&](Uint64 From, TGL::FPinRef To, float Weight) {
            const ImVec4 Dimmed{PoseColor.x, PoseColor.y, PoseColor.z, 0.38f};
            const ImVec4 Tint{!Playing ? PoseColor : Weight > 0.0f ? Mix(Dimmed, ActiveColor, 0.35f + 0.65f * Weight) : Dimmed};
            const TGL::FLinkId Id{To.mPin.Get()};
            TGL::FLinkView Link{Id, TGL::FPinRef{{From}, {OutputPinKey(From)}}, To, {}, {}, ImGui::ColorConvertFloat4ToU32(Tint), false, false};
            Link.mWidth = TGL::Canvas::LinkWidth + (Playing ? Weight : 0.0f);
            LinkViews.push_back(std::move(Link));

            if (Pulse && Weight > 0.0f) {
                ActiveLinks.push_back(Id);
            }
        }};

        // Output Pose 노드입니다. 루트 노드의 포즈가 최종 포즈가 됩니다.
        TGL::FNodeView Output{{OutputKey}, mGraph->mPositions.at(OutputKey), "Output Pose", {}, "Graph Output"};
        Output.mDeletable = false;
        Output.mPins.push_back({{OutputKey}, TGL::EPinDirection::Input, "Result", "Pose", {}, PoseTint});
        NodeViews.push_back(std::move(Output));

        if (Root != nullptr) {
            const float Weight{WeightOf(NodeWeights, Root)};
            AddPoseLink(NodeKey(Instance, Root), {{OutputKey}, {OutputKey}}, Weight);

            if (Weight > 0.0f) {
                ActiveNodes.push_back({OutputKey});
            }
        }

        std::unordered_set<Uint64> DrawnGetters{};

        for (Engine::FAnimNode_Base* Node : Nodes) {
            const Uint64 Key{NodeKey(Instance, Node)};
            const float Weight{WeightOf(NodeWeights, Node)};
            const auto* BlendSpacePlayer{dynamic_cast<const Engine::FAnimNode_BlendSpacePlayer*>(Node)};
            const Core::FString Type{Node->GetNodeType()};
            TGL::FNodeView View{{Key}, mGraph->mPositions.at(Key), Node->mName.empty() ? Type : Node->mName, {}, Type};
            View.mDeletable = false;
            View.mPins.push_back({{OutputPinKey(Key)}, TGL::EPinDirection::Output, "Pose", "Pose", {}, PoseTint});
            Core::TArray<Engine::FAnimNodePoseInput> Inputs{};
            Core::TArray<Engine::FAnimNodeValueInput> ValueInputs{};
            Core::TArray<Engine::FAnimNodeValue> Values{};
            Node->GetPoseInputs(Inputs);
            Node->GetValueInputs(ValueInputs);
            Node->GetDebugValues(Values);

            for (Index_t Index{}; Index < Inputs.size(); ++Index) {
                const auto& Input{Inputs[Index]};
                const float InputWeight{WeightOf(LinkWeights, Input.mLink)};
                const TGL::FPinId Pin{InputPinKey(Key, Index)};
                const Core::FString Label{Playing ? std::format("{}  {:.0f}%", Input.mName, InputWeight * 100.0f) : Input.mName};
                View.mPins.push_back({Pin, TGL::EPinDirection::Input, Label, "Pose", {}, PoseTint});

                if (Input.mLink != nullptr && Input.mLink->mLinkedNode != nullptr) {
                    AddPoseLink(NodeKey(Instance, Input.mLink->mLinkedNode), {View.mId, Pin}, InputWeight);
                }
            }

            if (Playing) {
                View.mProperties.push_back({"Weight", "Weight", std::format("{:.0f}%", Weight * 100.0f), {}, true});
                View.mProgress = Weight;

                if (Weight > 0.0f) {
                    View.mBorderColor = ImGui::ColorConvertFloat4ToU32(Mix(Style.mColors[TGL::Canvas::NodeBorder], ActiveColor, 0.55f * Weight));
                }
            }

            for (Index_t Index{}; Index < Values.size(); ++Index) {
                const auto& Value{Values[Index]};
                TGL::FPropertyView Property{std::format("Value{}", Index), Value.mName};
                Property.mReadOnly = !Playing;

                if (const auto* Float{std::get_if<Engine::FAnimNodeFloatValue>(&Value.mValue)}; Float != nullptr && Float->mValue != nullptr) {
                    if (BlendSpacePlayer != nullptr && Float->mValue == &BlendSpacePlayer->mX && BlendSpacePlayer->mBlendSpace != nullptr) {
                        const auto& Axis{BlendSpacePlayer->mBlendSpace->GetBlendParameter(Asset::EBlendSpaceAxis::X)};
                        Property.mName = std::format("{} ({})", Value.mName, Axis.mDisplayName);
                    }

                    const auto Input{std::ranges::find(ValueInputs, Value.mName, &Engine::FAnimNodeValueInput::mName)};
                    const auto* ValueNode{Input != ValueInputs.end() && Input->mLink != nullptr ? Input->mLink->GetLinkedNode() : nullptr};

                    if (ValueNode != nullptr) {
                        const TGL::FPinId Pin{FloatInputBit | InputPinKey(Key, Index)};
                        const Uint64 ValueKey{ValueNodeKeys.try_emplace(ValueNode, ValueNodeKeys.size() + 1).first->second};
                        const Uint64 SavedKey{Graph != nullptr ? Graph->GetValueNodeKey(ValueNode) : 0};
                        const Uint64 GetterKey{GetterBit | GraphKey | (SavedKey != 0 ? SavedGetterBit | (SavedKey << 8) : ValueKey << 8)};
                        const Core::FString Display{ValueNode->GetDisplayName()};
                        const Core::FString Text{std::format("{:.3f}", *Float->mValue)};
                        View.mPins.push_back({Pin, TGL::EPinDirection::Input, Playing ? Property.mName + "  " + Text : Property.mName, "Float", {}, FloatTint});
                        TGL::FLinkView Link{{Pin.Get()}, TGL::FPinRef{{GetterKey}, {OutputPinKey(GetterKey)}}, TGL::FPinRef{View.mId, Pin}, {}, {}, FloatTint, false, false};

                        if (Playing) {
                            Link.mTooltip = Display + " -> " + Property.mName + "  " + Text;
                        }

                        LinkViews.push_back(std::move(Link));

                        if (DrawnGetters.insert(GetterKey).second) {
                            const auto* Getter{Graph != nullptr ? Graph->GetSourceGetter(ValueNode) : nullptr};
                            const TGL::FPoint Position{Getter != nullptr ? TGL::FPoint{Getter->mX, Getter->mY} : TGL::FPoint{View.mPosition.mX - 245.0f, View.mPosition.mY - 170.0f}};
                            const auto [Location, Inserted]{mGraph->mPositions.try_emplace(GetterKey, Position)};
                            Placed |= Inserted;
                            TGL::FNodeView GetterView{{GetterKey}, Location->second, "Get " + Display, {}, "Float"};
                            GetterView.mDeletable = false;
                            GetterView.mPins.push_back({{OutputPinKey(GetterKey)}, TGL::EPinDirection::Output, Text, "Float", {}, FloatTint});
                            NodeViews.push_back(std::move(GetterView));
                        }

                        continue;
                    }

                    Property.mValue = static_cast<double>(*Float->mValue);
                    Property.mDisplayValue = std::format("{:.3f}", *Float->mValue);
                    Property.mInlineEditable = Playing;
                    Property.mNumeric = TGL::FNumericEditor{Float->mMin, Float->mMax, 0.01f, Float->mMin < Float->mMax};

                    if (Playing) {
                        LiveEdits.Bind(View.mId, Property.mKey, *Float);
                    }
                } else if (bool* const* Flag{std::get_if<bool*>(&Value.mValue)}; Flag != nullptr && *Flag != nullptr) {
                    Property.mValue = **Flag;
                    Property.mInlineEditable = Playing;

                    if (Playing) {
                        LiveEdits.Bind(View.mId, Property.mKey, *Flag);
                    }
                } else {
                    const auto* Text{std::get_if<Core::FString>(&Value.mValue)};
                    Property.mValue = Text != nullptr ? *Text : Core::FString{};
                    Property.mReadOnly = true;
                }

                View.mProperties.push_back(std::move(Property));
            }

            const auto RecordedSamples{SampleWeights.find(Node)};

            if (BlendSpacePlayer != nullptr && BlendSpacePlayer->mBlendSpace != nullptr && RecordedSamples != SampleWeights.end()) {
                const auto& Samples{BlendSpacePlayer->mBlendSpace->GetSamples()};

                for (const Asset::FBlendSampleWeight& SampleWeight : RecordedSamples->second) {
                    if (SampleWeight.mSampleIndex < Samples.size()) {
                        const auto& Sample{Samples[SampleWeight.mSampleIndex]};
                        const Core::FString Name{std::format("Sample {}", SampleWeight.mSampleIndex)};
                        const Core::FString Text{std::format("{} @ {:.2f} : {:.0f}%", Sample.mAnimation != nullptr ? Sample.mAnimation->GetName() : "?", Sample.mSampleValue.x, SampleWeight.mWeight * 100.0f)};
                        View.mProperties.push_back({Name, Name, Text, {}, true});
                    }
                }
            }

            NodeViews.push_back(std::move(View));
        }

        mGraph->mDocument.Replace(std::move(NodeViews), std::move(LinkViews));

        if (ResetLayout) {
            mGraph->mEditor.ResetLayout();
        } else if (Placed) {
            mGraph->mEditor.Fit();
        }

        // 그래프는 인스턴스가 만들므로 이 패널에서는 연결을 만들거나 지우지 않습니다. 편집은 그래프 에셋 편집 창에서 합니다.
        mGraph->mEditor.Draw("AnimGraphEditor", mGraph->mDocument, LiveEdits, Playing ? &LiveEdits : nullptr, {}, {ActiveNodes, ActiveLinks}, false);
    }

}
