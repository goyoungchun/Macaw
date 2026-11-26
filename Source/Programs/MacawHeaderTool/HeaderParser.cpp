/**
 * @file HeaderParser.cpp
 * @brief 리플렉션 표시를 파싱하고 선언·포함 순서 규칙을 검사합니다.
 */

#include "Core/Types.h"
#include "HeaderParser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace HeaderTool {

    /**
     * @brief 이 파일에서 사용하는 내부 보조 기능과 상태를 묶습니다.
     */
    namespace {

        /**
         * @brief 허용하는 리플렉션 표시 이름입니다.
         */
        constexpr std::string_view sAnnotations[]{"UCLASS", "USTRUCT", "UENUM", "UPROPERTY", "GENERATED_BODY"};

        /**
         * @brief 교체해야 하는 이전 리플렉션 매크로 이름입니다.
         */
        constexpr std::string_view sLegacyAnnotations[]{"ReflectTypes", "ReflectClass", "ReflectRootClass", "ReflectStruct", "ReflectMembers", "ReflectProperty", "ReflectAccessor", "ReflectSetter", "ReflectReadOnlyProperty", "ReflectReadOnlyAccessor", "ReflectArray", "ReflectArrayAccessor", "ReflectEnum"};

        /**
         * @brief 허용하는 프로퍼티 플래그 이름입니다.
         */
        constexpr std::string_view sPropertyFlags[]{"Edit", "Serialize", "ReadOnly", "Transient", "SerializeLate", "ShowOnlyInnerProperties"};

        /**
         * @brief 허용하는 클래스 플래그 이름입니다.
         */
        constexpr std::string_view sClassFlags[]{"Abstract", "NoFactory"};

        /**
         * @brief 고정 문자열 배열에 지정한 값이 있는지 검사합니다.
         * @tparam Count 고정 배열의 원소 수입니다.
         * @param[in] Values 검사하거나 ABI에 저장할 배열입니다.
         * @param[in] Value 검사·변환·저장할 값입니다.
         * @return 배열에 일치하는 문자열이 있으면 true입니다.
         */
        template <std::size_t Count>
        bool Contains(const std::string_view (&Values)[Count], std::string_view Value) {
            return std::find(std::begin(Values), std::end(Values), Value) != std::end(Values);
        }

        /**
         * @brief 네임스페이스 이름과 본문 토큰 범위입니다.
         */
        struct FNamespace {
            /**
             * @brief 본문 범위를 기록한 네임스페이스의 이름입니다.
             */
            std::string Name;

            /**
             * @brief 네임스페이스 본문의 여는 중괄호 토큰 위치입니다.
             */
            Index_t Opening{};

            /**
             * @brief 본문 또는 인자의 닫는 토큰 위치입니다.
             */
            Index_t Closing{};
        };

        /**
         * @brief 리플렉션 지정자의 이름과 선택적 값입니다.
         */
        struct FArgument {
            /**
             * @brief 리플렉션 지정자 이름입니다.
             */
            std::string Key;

            /**
             * @brief 등호 뒤의 원문 지정자 값입니다.
             */
            std::string Value;
        };

        /**
         * @brief 리플렉션 표시의 이름·토큰 범위·원문 인자입니다.
         */
        struct FAnnotation {
            /**
             * @brief UCLASS·UPROPERTY 등 리플렉션 표시의 이름입니다.
             */
            std::string Name;

            /**
             * @brief 표시 이름의 토큰 위치입니다.
             */
            Index_t Token{};

            /**
             * @brief 본문 또는 인자의 닫는 토큰 위치입니다.
             */
            Index_t Closing{};

            /**
             * @brief 표시의 괄호 안에서 분리한 원문 인자입니다.
             */
            std::vector<std::string> Arguments;
        };

        /**
         * @brief 범위 안의 토큰이 식별자인지 검사합니다.
         * @param[in] Source 원문·마스크·토큰을 제공하는 소스입니다.
         * @param[in] Index 처리할 원소 또는 토큰의 인덱스입니다.
         * @return 범위 안의 토큰이 식별자이면 true입니다.
         */
        bool IsIdentifier(const FSourceFile& Source, Index_t Index) {
            return Index < Source.Tokens.size() && Source.Tokens[Index].Kind == ETokenKind::Identifier;
        }

        // 괄호 안을 깊이 0의 쉼표로 나누어 원문 조각을 돌려줍니다.

        /**
         * @brief 괄호 내부를 중첩 깊이 0의 쉼표로 나누어 원문 인자를 수집합니다.
         * @param[in] Source 원문·마스크·토큰을 제공하는 소스입니다.
         * @param[in] Opening 여는 괄호 토큰 위치입니다.
         * @param[in] Closing 닫는 괄호 토큰 위치입니다.
         * @return 공백을 정리한 원문 인자 목록입니다.
         */
        std::vector<std::string> ReadArguments(const FSourceFile& Source, Index_t Opening, Index_t Closing) {
            std::vector<std::string> Arguments;
            Index_t First{static_cast<Index_t>(Opening + 1)};
            int Depth{};

            for (Index_t Index{static_cast<Index_t>(Opening + 1)}; Index <= Closing; ++Index) {
                const std::string& Text{Source.Tokens[Index].Text};

                if (Text == "(" || Text == "{" || Text == "[") {
                    ++Depth;
                } else if ((Text == ")" || Text == "}" || Text == "]") && Index != Closing) {
                    --Depth;
                }

                if (Index == Closing || (Text == "," && Depth == 0)) {
                    std::string Argument{Source.TextBetween(First, Index)};

                    if (!Argument.empty()) {
                        Arguments.push_back(std::move(Argument));
                    }

                    First = Index + 1;
                }
            }

            return Arguments;
        }

        /**
         * @brief 리플렉션 지정자를 첫 등호 기준으로 이름과 값으로 나눕니다.
         * @param[in] Text 해석·변환·탐색할 원문 문자열입니다.
         * @return 지정자 이름과 선택적인 값입니다.
         */
        FArgument SplitSpecifier(const std::string& Text) {
            const std::size_t Equal{Text.find('=')};

            if (Equal == std::string::npos) {
                return {Trim(Text), {}};
            }

            return {Trim(std::string_view{Text}.substr(0, Equal)), Trim(std::string_view{Text}.substr(Equal + 1))};
        }

        /**
         * @brief Meta = (...) 지정자의 괄호 안을 키와 값 리터럴 목록으로 나눕니다.
         * @param[in] Text 괄호를 포함한 지정자 값 원문입니다.
         * @param[out] Entries 키와 따옴표를 포함한 값 리터럴을 적은 순서대로 추가합니다.
         * @return 모든 항목이 올바르면 true, 괄호·키·값 형식이 잘못되었으면 false입니다.
         * @details 값은 따옴표 문자열을 그대로 쓰고, 식별자·숫자는 따옴표로 감쌉니다. 값을 생략하면 "true"입니다.
         */
        bool ParseMetaData(std::string_view Text, std::vector<std::pair<std::string, std::string>>& Entries) {
            const std::string Inner{Trim(Text)};

            if (Inner.size() < 2 || Inner.front() != '(' || Inner.back() != ')') {
                return false;
            }

            std::vector<std::string> Items;
            std::string Current;
            bool Quoted{};

            for (Index_t Index{1}; Index + 1 < Inner.size(); ++Index) {
                const char Character{Inner[Index]};

                if (Quoted && Character == '\\' && Index + 2 < Inner.size()) {
                    Current += Character;
                    Current += Inner[++Index];

                    continue;
                }

                if (Character == '"') {
                    Quoted = !Quoted;
                } else if (Character == ',' && !Quoted) {
                    Items.push_back(Trim(Current));
                    Current.clear();

                    continue;
                }

                Current += Character;
            }

            if (Quoted) {
                return false;
            }

            Items.push_back(Trim(Current));

            for (const std::string& Item : Items) {
                if (Item.empty()) {
                    continue;
                }

                const FArgument Entry{SplitSpecifier(Item)};

                if (Entry.Key.empty() || !std::all_of(Entry.Key.begin(), Entry.Key.end(), IsIdentifierCharacter)) {
                    return false;
                }

                std::string Value{Entry.Value};

                if (Value.empty()) {
                    Value = "\"true\"";
                } else if (Value.front() == '"') {
                    if (Value.size() < 2 || Value.back() != '"') {
                        return false;
                    }
                } else if (std::all_of(Value.begin(), Value.end(), [](char Character) {
                    return IsIdentifierCharacter(Character) || Character == '.' || Character == '-';
                })) {
                    Value = "\"" + Value + "\"";
                } else {
                    return false;
                }

                Entries.emplace_back(Entry.Key, std::move(Value));
            }

            return true;
        }

        /**
         * @brief 식별자에 사용할 수 없는 문자를 밑줄로 바꿉니다.
         * @param[in] Text 해석·변환·탐색할 원문 문자열입니다.
         * @return 해석·변환한 문자열입니다.
         */
        std::string Sanitize(std::string Text) {
            for (char& Character : Text) {
                if (!IsIdentifierCharacter(Character)) {
                    Character = '_';
                }
            }

            return Text;
        }

        /**
         * @brief 필드의 m 접두어를 제거해 기본 프로퍼티 이름을 구합니다.
         * @param[in] Member 프로퍼티 이름을 유도할 필드 이름입니다.
         * @return 해석·변환한 문자열입니다.
         */
        std::string DerivedName(const std::string& Member) {
            if (Member.size() > 1 && Member[0] == 'm' && std::isupper(static_cast<unsigned char>(Member[1]))) {
                return Member.substr(1);
            }

            return Member;
        }

        /**
         * @brief 헤더의 리플렉션 표시를 읽어 타입·프로퍼티·함수·열거형 정보를 구성합니다.
         */
        class FHeaderParser {
        public:
            /**
             * @brief 파싱할 헤더와 원본 소스·토큰을 연결합니다.
             * @param[in] Header 파싱하거나 생성에 참조할 헤더입니다.
             */
            explicit FHeaderParser(FHeader& Header)
                : mHeader{Header}, mSource{Header.Source} {
            }

            /**
             * @brief 타입과 리플렉션 표시를 읽고 generated.h 포함 규칙을 검증합니다.
             */
            void Parse() {
                ReadNamespaces();
                ReadDefinitions();

                for (const FAnnotation& Annotation : ReadAnnotations()) {
                    if (Annotation.Name == "UCLASS" || Annotation.Name == "USTRUCT") {
                        ReadType(Annotation);
                    } else if (Annotation.Name == "GENERATED_BODY") {
                        ReadGeneratedBody(Annotation);
                    } else if (Annotation.Name == "UPROPERTY") {
                        ReadProperty(Annotation);
                    } else {
                        ReadEnum(Annotation);
                    }
                }

                for (const FType& Type : mHeader.Types) {
                    if (Type.Kind != ETypeKind::None) {
                        if (Type.GeneratedBodyLine == 0) {
                            mSource.Fail(Type.Opening, Quote(Type.Name) + " has no GENERATED_BODY()");
                        }

                        mHeader.NeedsGeneratedHeader = true;
                    }
                }

                mHeader.NeedsGeneratedHeader = mHeader.NeedsGeneratedHeader || !mHeader.Enums.empty();

                const std::string Stem{std::filesystem::path{mSource.Path}.stem().string()};

                mHeader.GeneratedName = Stem + ".generated.h";
                mHeader.FileId = "MACAW_" + Sanitize(mSource.Module) + "_" + Sanitize(Stem) + "_h";

                CheckGeneratedInclude();
            }

        private:
            /**
             * @brief 진단에 넣을 이름을 작은따옴표로 감쌉니다.
             * @param[in] Text 해석·변환·탐색할 원문 문자열입니다.
             * @return 해석·변환한 문자열입니다.
             */
            static std::string Quote(const std::string& Text) {
                return "'" + Text + "'";
            }

            /**
             * @brief 이름이 있는 네임스페이스와 토큰 범위를 수집합니다.
             */
            void ReadNamespaces() {
                for (Index_t Index{}; Index < mSource.Tokens.size(); ++Index) {
                    if (!mSource.Is(Index, "namespace") || !IsIdentifier(mSource, Index + 1)) {
                        continue;
                    }

                    std::string Name{mSource.Tokens[Index + 1].Text};
                    Index_t Opening{static_cast<Index_t>(Index + 2)};

                    while (mSource.Is(Opening, "::") && IsIdentifier(mSource, Opening + 1)) {
                        Name += "::" + mSource.Tokens[Opening + 1].Text;
                        Opening += 2;
                    }

                    if (mSource.Is(Opening, "{")) {
                        mNamespaces.push_back({Name, Opening, mSource.FindClosing(Opening)});
                    }
                }
            }

            /**
             * @brief 토큰 위치를 포함하는 중첩 네임스페이스 이름을 조합합니다.
             * @param[in] Index 처리할 원소 또는 토큰의 인덱스입니다.
             * @return 해석·변환한 문자열입니다.
             */
            std::string NamespaceAt(Index_t Index) const {
                std::string Result;

                for (const FNamespace& Namespace : mNamespaces) {
                    if (Index > Namespace.Opening && Index < Namespace.Closing) {
                        Result += (Result.empty() ? "" : "::") + Namespace.Name;
                    }
                }

                return Result;
            }

            /**
             * @brief 타입 키워드 앞에 템플릿 매개변수 목록이 있는지 검사합니다.
             * @param[in] Keyword 템플릿 여부를 검사할 타입 키워드 위치입니다.
             * @return 템플릿 매개변수 목록이 앞에 있으면 true입니다.
             */
            bool IsTemplateHead(Index_t Keyword) const {
                if (Keyword == 0 || !mSource.Is(Keyword - 1, ">")) {
                    return false;
                }

                int Depth{};

                for (Index_t Index{static_cast<Index_t>(Keyword - 1)}; Index > 0; --Index) {
                    if (mSource.Is(Index, ">")) {
                        ++Depth;
                    } else if (mSource.Is(Index, "<") && --Depth == 0) {
                        return mSource.Is(Index - 1, "template");
                    }
                }

                return false;
            }

            /**
             * @brief 클래스·구조체 정의와 본문 범위를 수집합니다.
             */
            void ReadDefinitions() {
                const std::vector<FToken>& Tokens{mSource.Tokens};

                for (Index_t Index{}; Index < Tokens.size(); ++Index) {
                    if ((!mSource.Is(Index, "class") && !mSource.Is(Index, "struct")) || (Index > 0 && mSource.Is(Index - 1, "enum"))) {
                        continue;
                    }

                    Index_t NameIndex{static_cast<Index_t>(Index + 1)};

                    if (mSource.Is(NameIndex, "alignas") && mSource.Is(NameIndex + 1, "(")) {
                        NameIndex = mSource.FindClosing(NameIndex + 1) + 1;
                    }

                    if (!IsIdentifier(mSource, NameIndex)) {
                        continue;
                    }

                    Index_t Opening{static_cast<Index_t>(NameIndex + 1)};

                    while (Opening < Tokens.size() && !mSource.Is(Opening, "{") && !mSource.Is(Opening, ";")) {
                        ++Opening;
                    }

                    if (!mSource.Is(Opening, "{")) {
                        continue;
                    }

                    FType Type;
                    Type.ShortName = Tokens[NameIndex].Text;
                    Type.Keyword = Tokens[Index].Text;
                    Type.Namespace = NamespaceAt(Index);
                    Type.Opening = Opening;
                    Type.Closing = mSource.FindClosing(Opening);
                    Type.File = mSource.Path;
                    Type.SourceModule = mSource.Module;

                    for (Index_t Outer{}; Outer < mHeader.Types.size(); ++Outer) {
                        if (Opening > mHeader.Types[Outer].Opening && Opening < mHeader.Types[Outer].Closing) {
                            Type.Outer = static_cast<Int32>(Outer);
                        }
                    }

                    const FType* Outer{Type.Outer >= 0 ? &mHeader.Types[static_cast<Index_t>(Type.Outer)] : nullptr};

                    Type.Name = Outer != nullptr ? Outer->Name + "::" + Type.ShortName : Type.ShortName;
                    Type.Template = IsTemplateHead(Index) || (Outer != nullptr && Outer->Template);
                    Type.Super = ReadSuper(NameIndex + 1, Opening);

                    mHeader.Types.push_back(std::move(Type));
                    mKeywords.push_back(Index);
                }
            }

            /**
             * @brief 타입 선언에서 지원하는 단일 기반 클래스 이름을 읽습니다.
             * @param[in] Index 처리할 원소 또는 토큰의 인덱스입니다.
             * @param[in] Opening 여는 괄호 토큰 위치입니다.
             * @return 정규화한 기반 클래스 이름이며 상속이 없으면 빈 문자열입니다.
             */
            std::string ReadSuper(Index_t Index, Index_t Opening) const {
                if (mSource.Is(Index, "final")) {
                    ++Index;
                }

                if (!mSource.Is(Index, ":")) {
                    return {};
                }

                ++Index;

                while (mSource.Is(Index, "public") || mSource.Is(Index, "protected") || mSource.Is(Index, "private") || mSource.Is(Index, "virtual")) {
                    ++Index;
                }

                Index_t Last{Index};
                int Depth{};

                for (; Last < Opening; ++Last) {
                    const std::string& Text{mSource.Tokens[Last].Text};

                    Depth += (Text == "<" || Text == "(") ? 1 : ((Text == ">" || Text == ")") ? -1 : 0);

                    if (Text == "," && Depth == 0) {
                        break;
                    }
                }

                return mSource.TextBetween(Index, Last);
            }

            /**
             * @brief 리플렉션 표시와 인자를 수집하고 이전 매크로 사용을 거부합니다.
             * @return 검증한 리플렉션 표시 목록입니다.
             */
            std::vector<FAnnotation> ReadAnnotations() const {
                std::vector<FAnnotation> Annotations;

                for (Index_t Index{}; Index < mSource.Tokens.size(); ++Index) {
                    const FToken& Token{mSource.Tokens[Index]};

                    if (Token.Kind != ETokenKind::Identifier || !mSource.Is(Index + 1, "(")) {
                        continue;
                    }

                    if (Contains(sLegacyAnnotations, Token.Text)) {
                        mSource.Fail(Index, Token.Text + " is no longer supported; use UCLASS, USTRUCT, UENUM, UPROPERTY and GENERATED_BODY");
                    }

                    if (Contains(sAnnotations, Token.Text)) {
                        const Index_t Closing{static_cast<Index_t>(mSource.FindClosing(Index + 1))};

                        Annotations.push_back({Token.Text, Index, Closing, ReadArguments(mSource, Index + 1, Closing)});

                        Index = Closing;
                    }
                }

                return Annotations;
            }

            /**
             * @brief 토큰 위치를 본문에 포함하는 가장 안쪽 타입을 찾습니다.
             * @param[in] Index 처리할 원소 또는 토큰의 인덱스입니다.
             * @return 위치를 포함하는 가장 안쪽 타입이며 없으면 nullptr입니다.
             */
            FType* OwnerAt(Index_t Index) {
                FType* Owner{};

                for (FType& Type : mHeader.Types) {
                    if (Index > Type.Opening && Index < Type.Closing) {
                        Owner = &Type;
                    }
                }

                return Owner;
            }

            /**
             * @brief 표시가 속한 타입을 찾고 리플렉션 대상으로 선언되었는지 검사합니다.
             * @param[in] Annotation 해석할 리플렉션 표시·토큰 범위입니다.
             * @return 리플렉션 표시를 소유한 타입 참조입니다.
             */
            FType& ReflectedOwner(const FAnnotation& Annotation) {
                FType* Owner{OwnerAt(Annotation.Token)};

                if (Owner == nullptr || Owner->Kind == ETypeKind::None) {
                    mSource.Fail(Annotation.Token, Annotation.Name + " must be inside a UCLASS() or USTRUCT() type");
                }

                if (Owner->Template) {
                    mSource.Fail(Annotation.Token, "Template types cannot use reflection");
                }

                return *Owner;
            }

            /**
             * @brief 리플렉션 표시를 타입 정의에 연결하고 종류와 플래그를 기록합니다.
             * @param[in] Annotation 해석할 리플렉션 표시·토큰 범위입니다.
             */
            void ReadType(const FAnnotation& Annotation) {
                const auto Found{std::find(mKeywords.begin(), mKeywords.end(), Annotation.Closing + 1)};

                if (Found == mKeywords.end()) {
                    mSource.Fail(Annotation.Token, Annotation.Name + "() must be followed by a class or struct definition");
                }

                FType& Type{mHeader.Types[static_cast<std::size_t>(Found - mKeywords.begin())]};

                if (Type.Template) {
                    mSource.Fail(Annotation.Token, "Template types cannot use reflection");
                }

                NoteReflected(Annotation);

                if (Annotation.Name == "USTRUCT") {
                    if (!Annotation.Arguments.empty()) {
                        mSource.Fail(Annotation.Token, "USTRUCT() takes no specifiers");
                    }

                    Type.Kind = ETypeKind::Struct;

                    return;
                }

                Type.Kind = Type.Super.empty() ? ETypeKind::RootClass : ETypeKind::Class;

                for (const std::string& Argument : Annotation.Arguments) {
                    if (!Contains(sClassFlags, Argument)) {
                        mSource.Fail(Annotation.Token, "Unknown UCLASS specifier " + Quote(Argument));
                    }

                    Type.ClassFlags.push_back("CoreUObject::EClassFlags::" + Argument);
                }
            }

            /**
             * @brief GENERATED_BODY 위치를 소유 타입에 기록하고 중복을 검사합니다.
             * @param[in] Annotation 해석할 리플렉션 표시·토큰 범위입니다.
             */
            void ReadGeneratedBody(const FAnnotation& Annotation) {
                FType& Owner{ReflectedOwner(Annotation)};

                if (!Annotation.Arguments.empty()) {
                    mSource.Fail(Annotation.Token, "GENERATED_BODY() takes no arguments");
                }

                if (Owner.GeneratedBodyLine != 0) {
                    mSource.Fail(Annotation.Token, Quote(Owner.Name) + " has GENERATED_BODY() twice");
                }

                Owner.GeneratedBodyLine = mSource.LineAt(mSource.Tokens[Annotation.Token].Offset);
            }

            /**
             * @brief 필드와 지정자를 읽어 접근자·직렬화·배열 등록 정보를 구성합니다.
             * @param[in] Annotation 해석할 리플렉션 표시·토큰 범위입니다.
             */
            void ReadProperty(const FAnnotation& Annotation) {
                FType& Owner{ReflectedOwner(Annotation)};
                const Index_t First{static_cast<Index_t>(Annotation.Closing + 1)};
                Index_t Index{First};
                int Angles{};

                for (; Index < mSource.Tokens.size(); ++Index) {
                    const std::string& Text{mSource.Tokens[Index].Text};

                    if (Text == "<") {
                        ++Angles;
                    } else if (Text == ">") {
                        --Angles;
                    } else if (Angles == 0 && (Text == "{" || Text == "=" || Text == "[" || Text == ";")) {
                        break;
                    } else if ((Angles == 0 && Text == "(") || Text == "}" || Contains(sAnnotations, Text)) {
                        mSource.Fail(Annotation.Token, "UPROPERTY() must be followed by a member variable declaration");
                    }
                }

                if (Index == First || !IsIdentifier(mSource, Index - 1)) {
                    mSource.Fail(Annotation.Token, "UPROPERTY() must be followed by a member variable declaration");
                }

                FProperty Property;
                Property.Member = mSource.Tokens[Index - 1].Text;

                const std::string Type{mSource.TextBetween(First, Index - 1)};

                if (Type.empty() || ContainsWord(Type, "static")) {
                    mSource.Fail(Annotation.Token, "UPROPERTY() requires a non-static member variable");
                }

                Property.Array = Type.starts_with("Core::TArray<") || Type.starts_with("TArray<");

                for (const std::string& Argument : Annotation.Arguments) {
                    const FArgument Specifier{SplitSpecifier(Argument)};

                    if (Specifier.Key == "Getter" && !Specifier.Value.empty()) {
                        Property.Getter = Specifier.Value;
                    } else if (Specifier.Key == "Setter" && !Specifier.Value.empty()) {
                        Property.Setter = Specifier.Value;
                    } else if (Specifier.Key == "Name" && Specifier.Value.size() > 1 && Specifier.Value.front() == '"' && Specifier.Value.back() == '"') {
                        Property.Name = Specifier.Value;
                    } else if (Specifier.Key == "Category" && Specifier.Value.size() > 2 && Specifier.Value.front() == '"' && Specifier.Value.back() == '"') {
                        Property.MetaData.emplace_back("Category", Specifier.Value);
                    } else if (Specifier.Key == "Meta" && !Specifier.Value.empty()) {
                        if (!ParseMetaData(Specifier.Value, Property.MetaData)) {
                            mSource.Fail(Annotation.Token, "UPROPERTY Meta must be (Key = \"Value\", ...) " + Quote(Argument));
                        }
                    } else if (Specifier.Key == "Array" && Specifier.Value.empty()) {
                        Property.Array = true;
                    } else if (Specifier.Value.empty() && Contains(sPropertyFlags, Specifier.Key)) {
                        Property.Flags.push_back(Specifier.Key);
                    } else {
                        mSource.Fail(Annotation.Token, "Unknown UPROPERTY specifier " + Quote(Argument));
                    }
                }

                if (!Property.Getter.empty() && Property.Setter.empty()) {
                    mSource.Fail(Annotation.Token, "UPROPERTY Getter requires a Setter");
                }

                if (Property.Array && Property.Getter.empty() && !Property.Setter.empty()) {
                    mSource.Fail(Annotation.Token, "Array UPROPERTY Setter requires a Getter");
                }

                if (Property.Name.empty()) {
                    Property.Name = "\"" + DerivedName(Property.Member) + "\"";
                }

                for (Index_t Entry{}; Entry < Property.MetaData.size(); ++Entry) {
                    for (Index_t Other{}; Other < Entry; ++Other) {
                        if (Property.MetaData[Other].first == Property.MetaData[Entry].first) {
                            mSource.Fail(Annotation.Token, "Duplicate UPROPERTY metadata key " + Quote(Property.MetaData[Entry].first));
                        }
                    }
                }

                for (const FProperty& Existing : Owner.Properties) {
                    if (Existing.Member == Property.Member) {
                        mSource.Fail(Annotation.Token, "Duplicate reflected member " + Quote(Property.Member));
                    }
                }

                Owner.Properties.push_back(std::move(Property));
            }

            /**
             * @brief 열거형 표시와 값을 읽어 리플렉션 정보를 구성합니다.
             * @param[in] Annotation 해석할 리플렉션 표시·토큰 범위입니다.
             */
            void ReadEnum(const FAnnotation& Annotation) {
                if (OwnerAt(Annotation.Token) != nullptr) {
                    mSource.Fail(Annotation.Token, "UENUM() must be at namespace scope");
                }

                FEnum Enum;
                Enum.File = mSource.Path;
                Enum.Namespace = NamespaceAt(Annotation.Token);

                if (!Annotation.Arguments.empty()) {
                    mSource.Fail(Annotation.Token, "UENUM() does not accept arguments");
                }

                NoteReflected(Annotation);

                Index_t Index{static_cast<Index_t>(Annotation.Closing + 1)};

                if (!mSource.Is(Index, "enum")) {
                    mSource.Fail(Annotation.Token, "UENUM() must be followed by an enum definition");
                }

                ++Index;
                Enum.Scoped = mSource.Is(Index, "class") || mSource.Is(Index, "struct");
                Index += Enum.Scoped ? 1 : 0;

                if (!IsIdentifier(mSource, Index)) {
                    mSource.Fail(Annotation.Token, "UENUM() requires a named enum");
                }

                Enum.Name = mSource.Tokens[Index].Text;

                Index_t Opening{static_cast<Index_t>(Index + 1)};

                if (mSource.Is(Opening, ":")) {
                    while (Opening < mSource.Tokens.size() && !mSource.Is(Opening, "{") && !mSource.Is(Opening, ";")) {
                        ++Opening;
                    }

                    Enum.Base = mSource.TextBetween(Index + 2, Opening);
                }

                if (!mSource.Is(Opening, "{")) {
                    mSource.Fail(Annotation.Token, "UENUM() must be followed by an enum definition");
                }

                if (!Enum.Scoped && Enum.Base.empty()) {
                    mSource.Fail(Annotation.Token, "Reflected unscoped enums need an underlying type so they can be forward declared");
                }

                const Index_t Closing{static_cast<Index_t>(mSource.FindClosing(Opening))};
                std::set<std::string> Seen;
                Index_t First{static_cast<Index_t>(Opening + 1)};
                int Depth{};

                for (Index_t Entry{static_cast<Index_t>(Opening + 1)}; Entry <= Closing; ++Entry) {
                    const std::string& Text{mSource.Tokens[Entry].Text};

                    Depth += Text == "(" ? 1 : (Text == ")" ? -1 : 0);

                    if (Entry != Closing && (Text != "," || Depth != 0)) {
                        continue;
                    }

                    if (Entry > First) {
                        if (!IsIdentifier(mSource, First)) {
                            mSource.Fail(First, "Invalid enum value");
                        }

                        bool Hidden{};

                        for (Index_t Meta{First}; Meta < Entry; ++Meta) {
                            if (mSource.Is(Meta, "UMETA") && mSource.Is(Meta + 1, "(")) {
                                const Index_t MetaClosing{static_cast<Index_t>(mSource.FindClosing(Meta + 1))};

                                for (const std::string& Argument : ReadArguments(mSource, Meta + 1, MetaClosing)) {
                                    if (Argument != "Hidden") {
                                        mSource.Fail(Meta, "UMETA() accepts only Hidden");
                                    }

                                    Hidden = true;
                                }
                            }
                        }

                        const std::string& Value{mSource.Tokens[First].Text};

                        if (!Seen.insert(Value).second) {
                            mSource.Fail(First, "Duplicate enum value " + Quote(Value));
                        }

                        if (!Hidden) {
                            Enum.Values.push_back(Value);
                        }
                    }

                    First = Entry + 1;
                }

                mHeader.Enums.push_back(std::move(Enum));
            }

            // 뒤에 오는 헤더의 generated.h가 CURRENT_FILE_ID를 덮어쓰므로 첫 리플렉션 타입 앞의 마지막 #include여야 합니다.

            /**
             * @brief 첫 리플렉션 타입 앞의 마지막 include가 자기 generated.h인지 검증합니다.
             */
            void CheckGeneratedInclude() const {
                std::string Last;
                int LastLine{};
                std::size_t Position{};
                int Line{1};

                while (Position < mSource.Text.size()) {
                    std::size_t End{mSource.Text.find('\n', Position)};

                    End = End == std::string::npos ? mSource.Text.size() : End;

                    const std::string Text{Trim(std::string_view{mSource.Text}.substr(Position, End - Position))};
                    const std::string Directive{Text.starts_with("#") ? Trim(std::string_view{Text}.substr(1)) : std::string{}};
                    const std::string Target{Directive.starts_with("include") ? Trim(std::string_view{Directive}.substr(7)) : std::string{}};

                    if (Target.size() > 2) {
                        const std::string Name{std::filesystem::path{Target.substr(1, Target.size() - 2)}.filename().string()};

                        if (!mHeader.NeedsGeneratedHeader && Name.ends_with(".generated.h")) {
                            throw FHeaderToolError{mSource.Path, Line, Name + " is included but nothing in this header is reflected"};
                        }

                        if (mFirstReflectedLine == 0 || Line < mFirstReflectedLine) {
                            Last = Name;
                            LastLine = Line;
                        }
                    }

                    Position = End + 1;
                    ++Line;
                }

                if (mHeader.NeedsGeneratedHeader && Last != mHeader.GeneratedName) {
                    throw FHeaderToolError{mSource.Path, LastLine, "The last #include before the first reflected type must be \"" + mHeader.GeneratedName + "\""};
                }
            }

            /**
             * @brief 첫 리플렉션 표시의 줄 번호를 기록합니다.
             * @param[in] Annotation 해석할 리플렉션 표시·토큰 범위입니다.
             */
            void NoteReflected(const FAnnotation& Annotation) {
                const int Line{mSource.LineAt(mSource.Tokens[Annotation.Token].Offset)};

                if (mFirstReflectedLine == 0 || Line < mFirstReflectedLine) {
                    mFirstReflectedLine = Line;
                }
            }

            /**
             * @brief 파싱 결과를 기록할 헤더 참조입니다.
             */
            FHeader& mHeader;

            /**
             * @brief 파싱할 원문·마스크·토큰의 참조입니다.
             */
            const FSourceFile& mSource;

            /**
             * @brief 네임스페이스 이름·토큰 범위 목록입니다.
             */
            std::vector<FNamespace> mNamespaces;

            /**
             * @brief 클래스·구조체 시작 키워드의 토큰 위치입니다.
             */
            std::vector<Index_t> mKeywords;

            /**
             * @brief 첫 리플렉션 표시의 줄 번호입니다.
             */
            int mFirstReflectedLine{};
        };

    }

    /**
     * @brief 헤더를 파싱합니다.
     */
    std::unique_ptr<FHeader> ParseHeader(FSourceFile Source) {
        auto Header{std::make_unique<FHeader>()};
        Header->Source = std::move(Source);

        FHeaderParser{*Header}.Parse();

        return Header;
    }

    /**
     * @brief 소스 파일에 리플렉션 어노테이션이 없는지 검사합니다.
     */
    void CheckSourceFile(const FSourceFile& Source) {
        for (Index_t Index{}; Index < Source.Tokens.size(); ++Index) {
            const FToken& Token{Source.Tokens[Index]};

            if (Token.Kind != ETokenKind::Identifier || !Source.Is(Index + 1, "(")) {
                continue;
            }

            if (Contains(sAnnotations, Token.Text) || Contains(sLegacyAnnotations, Token.Text)) {
                Source.Fail(Index, "Move reflection annotations into headers: " + Token.Text);
            }

            if (Token.Text.starts_with("Implement") && Token.Text.ends_with("Reflection")) {
                Source.Fail(Index, "Reflection implementations must be generated from header annotations");
            }
        }
    }

}
