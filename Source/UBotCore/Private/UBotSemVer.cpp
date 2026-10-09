#include "UBotSemVer.h"

// Named rather than anonymous so unity builds cannot merge it with other files' helpers.
namespace UBot::SemVerPrivate
{
    bool IsDigitChar(TCHAR Char)
    {
        return Char >= TEXT('0') && Char <= TEXT('9');
    }

    bool IsIdentifierChar(TCHAR Char)
    {
        return IsDigitChar(Char)
            || (Char >= TEXT('a') && Char <= TEXT('z'))
            || (Char >= TEXT('A') && Char <= TEXT('Z'))
            || Char == TEXT('-');
    }

    bool IsNumericIdentifier(const FString& Identifier)
    {
        if (Identifier.IsEmpty())
        {
            return false;
        }
        for (const TCHAR Char : Identifier)
        {
            if (!IsDigitChar(Char))
            {
                return false;
            }
        }
        return true;
    }

    bool ParseComponent(const FString& Text, int32& OutValue)
    {
        if (Text.IsEmpty())
        {
            return false;
        }

        int64 Value = 0;
        for (const TCHAR Char : Text)
        {
            if (!IsDigitChar(Char))
            {
                return false;
            }
            Value = Value * 10 + (Char - TEXT('0'));
            if (Value > MAX_int32)
            {
                return false;
            }
        }

        OutValue = static_cast<int32>(Value);
        return true;
    }

    bool IsValidPreRelease(const FString& PreRelease)
    {
        if (PreRelease.IsEmpty())
        {
            return false;
        }

        TArray<FString> Identifiers;
        PreRelease.ParseIntoArray(Identifiers, TEXT("."), false);
        for (const FString& Identifier : Identifiers)
        {
            if (Identifier.IsEmpty())
            {
                return false;
            }
            for (const TCHAR Char : Identifier)
            {
                if (!IsIdentifierChar(Char))
                {
                    return false;
                }
            }
        }
        return true;
    }

    // Shared by FUBotSemVer::Parse and the constraint parser, which needs to know how many release
    // components were written to expand partial versions.
    bool ParseVersion(const FString& InText, FUBotSemVer& Out, int32& OutComponentCount)
    {
        FString Text = InText.TrimStartAndEnd();
        if (Text.StartsWith(TEXT("v"), ESearchCase::IgnoreCase))
        {
            Text = Text.RightChop(1);
        }

        int32 PlusIndex = INDEX_NONE;
        if (Text.FindChar(TEXT('+'), PlusIndex))
        {
            Text = Text.Left(PlusIndex);
        }

        FString Release = Text;
        FString PreRelease;
        int32 DashIndex = INDEX_NONE;
        if (Text.FindChar(TEXT('-'), DashIndex))
        {
            Release = Text.Left(DashIndex);
            PreRelease = Text.Mid(DashIndex + 1);
            if (!IsValidPreRelease(PreRelease))
            {
                return false;
            }
        }

        TArray<FString> Components;
        Release.ParseIntoArray(Components, TEXT("."), false);
        if (Components.Num() < 1 || Components.Num() > 3)
        {
            return false;
        }

        int32 Values[3] = { 0, 0, 0 };
        for (int32 Index = 0; Index < Components.Num(); ++Index)
        {
            if (!ParseComponent(Components[Index], Values[Index]))
            {
                return false;
            }
        }

        Out = FUBotSemVer(Values[0], Values[1], Values[2], PreRelease);
        OutComponentCount = Components.Num();
        return true;
    }

    int32 Sign(int32 Value)
    {
        return Value < 0 ? -1 : (Value > 0 ? 1 : 0);
    }

    int32 CompareNumericIdentifiers(const FString& A, const FString& B)
    {
        // Compare as digit strings so arbitrarily long identifiers cannot overflow.
        int32 StartA = 0;
        while (StartA < A.Len() - 1 && A[StartA] == TEXT('0'))
        {
            ++StartA;
        }
        int32 StartB = 0;
        while (StartB < B.Len() - 1 && B[StartB] == TEXT('0'))
        {
            ++StartB;
        }

        const int32 LengthA = A.Len() - StartA;
        const int32 LengthB = B.Len() - StartB;
        if (LengthA != LengthB)
        {
            return LengthA < LengthB ? -1 : 1;
        }
        for (int32 Offset = 0; Offset < LengthA; ++Offset)
        {
            const TCHAR CharA = A[StartA + Offset];
            const TCHAR CharB = B[StartB + Offset];
            if (CharA != CharB)
            {
                return CharA < CharB ? -1 : 1;
            }
        }
        return 0;
    }

    int32 ComparePreRelease(const FString& A, const FString& B)
    {
        if (A.IsEmpty() || B.IsEmpty())
        {
            // A release version has higher precedence than any of its prereleases.
            return A.IsEmpty() == B.IsEmpty() ? 0 : (A.IsEmpty() ? 1 : -1);
        }

        TArray<FString> IdentifiersA;
        TArray<FString> IdentifiersB;
        A.ParseIntoArray(IdentifiersA, TEXT("."), false);
        B.ParseIntoArray(IdentifiersB, TEXT("."), false);

        const int32 CommonCount = FMath::Min(IdentifiersA.Num(), IdentifiersB.Num());
        for (int32 Index = 0; Index < CommonCount; ++Index)
        {
            const FString& IdA = IdentifiersA[Index];
            const FString& IdB = IdentifiersB[Index];
            const bool bNumericA = IsNumericIdentifier(IdA);
            const bool bNumericB = IsNumericIdentifier(IdB);

            int32 Result = 0;
            if (bNumericA && bNumericB)
            {
                Result = CompareNumericIdentifiers(IdA, IdB);
            }
            else if (bNumericA != bNumericB)
            {
                // Numeric identifiers always have lower precedence than alphanumeric ones.
                Result = bNumericA ? -1 : 1;
            }
            else
            {
                Result = Sign(IdA.Compare(IdB, ESearchCase::CaseSensitive));
            }

            if (Result != 0)
            {
                return Result;
            }
        }

        return Sign(IdentifiersA.Num() - IdentifiersB.Num());
    }

    // Smallest version above every version that shares Version's components up to ComponentIndex
    // (0 = major, 1 = minor, 2 = patch). The "-0" prerelease is the lowest possible one, so
    // "< X.Y.Z-0" also rejects the prereleases of X.Y.Z. Returns false when the component is
    // already at its maximum, i.e. there is no such version.
    bool MakeUpperBound(const FUBotSemVer& Version, int32 ComponentIndex, FUBotSemVer& OutBound)
    {
        int32 Values[3] = { Version.Major, Version.Minor, Version.Patch };
        if (Values[ComponentIndex] == MAX_int32)
        {
            return false;
        }

        ++Values[ComponentIndex];
        for (int32 Index = ComponentIndex + 1; Index < 3; ++Index)
        {
            Values[Index] = 0;
        }

        OutBound = FUBotSemVer(Values[0], Values[1], Values[2], TEXT("0"));
        return true;
    }
}

FUBotSemVer::FUBotSemVer(int32 InMajor, int32 InMinor, int32 InPatch, const FString& InPreRelease)
    : Major(InMajor)
    , Minor(InMinor)
    , Patch(InPatch)
    , PreRelease(InPreRelease)
{
}

bool FUBotSemVer::Parse(const FString& Text, FUBotSemVer& Out)
{
    int32 ComponentCount = 0;
    return UBot::SemVerPrivate::ParseVersion(Text, Out, ComponentCount);
}

int32 FUBotSemVer::Compare(const FUBotSemVer& Other) const
{
    if (Major != Other.Major)
    {
        return Major < Other.Major ? -1 : 1;
    }
    if (Minor != Other.Minor)
    {
        return Minor < Other.Minor ? -1 : 1;
    }
    if (Patch != Other.Patch)
    {
        return Patch < Other.Patch ? -1 : 1;
    }
    return UBot::SemVerPrivate::ComparePreRelease(PreRelease, Other.PreRelease);
}

FString FUBotSemVer::ToString() const
{
    FString Result = FString::Printf(TEXT("%d.%d.%d"), Major, Minor, Patch);
    if (!PreRelease.IsEmpty())
    {
        Result += TEXT("-");
        Result += PreRelease;
    }
    return Result;
}

bool FUBotVersionConstraint::Parse(const FString& InText, FUBotVersionConstraint& Out, FString* OutError)
{
    using namespace UBot::SemVerPrivate;

    auto Fail = [OutError](const FString& Message)
    {
        if (OutError)
        {
            *OutError = Message;
        }
        return false;
    };

    // Two character operators first so ">=" is not read as ">" followed by "=1.0".
    static const TCHAR* const Operators[] = { TEXT(">="), TEXT("<="), TEXT(">"), TEXT("<"), TEXT("="), TEXT("^"), TEXT("~") };

    TArray<FString> Tokens;
    InText.ParseIntoArrayWS(Tokens);

    FUBotVersionConstraint Result;
    TArray<FString> NormalizedTokens;

    for (int32 TokenIndex = 0; TokenIndex < Tokens.Num(); ++TokenIndex)
    {
        const FString& Token = Tokens[TokenIndex];
        if (Token == TEXT("*"))
        {
            NormalizedTokens.Add(Token);
            continue;
        }

        FString Operator;
        for (const TCHAR* Candidate : Operators)
        {
            if (Token.StartsWith(Candidate, ESearchCase::CaseSensitive))
            {
                Operator = Candidate;
                break;
            }
        }

        FString VersionText = Token.RightChop(Operator.Len());
        if (VersionText.IsEmpty())
        {
            if (TokenIndex + 1 >= Tokens.Num())
            {
                return Fail(FString::Printf(TEXT("Operator '%s' has no version in constraint '%s'"), *Operator, *InText));
            }
            VersionText = Tokens[++TokenIndex];
        }

        FUBotSemVer Version;
        int32 ComponentCount = 0;
        if (!ParseVersion(VersionText, Version, ComponentCount))
        {
            return Fail(FString::Printf(TEXT("Invalid version '%s' in constraint '%s'"), *VersionText, *InText));
        }

        // A prerelease pins an exact version, so "1.2-beta" is treated like "1.2.0-beta".
        const int32 EffectiveCount = Version.IsPreRelease() ? 3 : ComponentCount;
        const bool bPartial = EffectiveCount < 3;
        const int32 LastComponent = EffectiveCount - 1;

        auto AddComparator = [&Result](EComparison Comparison, const FUBotSemVer& Bound)
        {
            FComparator& Comparator = Result.Comparators.AddDefaulted_GetRef();
            Comparator.Comparison = Comparison;
            Comparator.Version = Bound;
        };
        auto AddUpperBound = [&AddComparator, &Version](int32 ComponentIndex)
        {
            FUBotSemVer Bound;
            if (MakeUpperBound(Version, ComponentIndex, Bound))
            {
                AddComparator(EComparison::Less, Bound);
            }
        };

        if (Operator == TEXT("^"))
        {
            // Bump the left-most non-zero component; when every written component is zero, bump the
            // last written one (^0 -> <1.0.0, ^0.0 -> <0.1.0, ^0.0.0 -> <0.0.1).
            int32 BumpIndex = 2;
            if (Version.Major != 0 || EffectiveCount == 1)
            {
                BumpIndex = 0;
            }
            else if (Version.Minor != 0 || EffectiveCount == 2)
            {
                BumpIndex = 1;
            }
            AddComparator(EComparison::GreaterOrEqual, Version);
            AddUpperBound(BumpIndex);
        }
        else if (Operator == TEXT("~"))
        {
            AddComparator(EComparison::GreaterOrEqual, Version);
            AddUpperBound(EffectiveCount == 1 ? 0 : 1);
        }
        else if (Operator == TEXT(">="))
        {
            AddComparator(EComparison::GreaterOrEqual, Version);
        }
        else if (Operator == TEXT(">"))
        {
            if (bPartial)
            {
                // Greater than every release of the X-range: at least the next release.
                FUBotSemVer Bound;
                if (MakeUpperBound(Version, LastComponent, Bound))
                {
                    Bound.PreRelease.Reset();
                    AddComparator(EComparison::GreaterOrEqual, Bound);
                }
                else
                {
                    AddComparator(EComparison::Greater, FUBotSemVer(MAX_int32, MAX_int32, MAX_int32));
                }
            }
            else
            {
                AddComparator(EComparison::Greater, Version);
            }
        }
        else if (Operator == TEXT("<"))
        {
            if (bPartial)
            {
                AddComparator(EComparison::Less, FUBotSemVer(Version.Major, Version.Minor, Version.Patch, TEXT("0")));
            }
            else
            {
                AddComparator(EComparison::Less, Version);
            }
        }
        else if (Operator == TEXT("<="))
        {
            if (bPartial)
            {
                AddUpperBound(LastComponent);
            }
            else
            {
                AddComparator(EComparison::LessOrEqual, Version);
            }
        }
        else
        {
            // "=" or no operator.
            if (bPartial)
            {
                AddComparator(EComparison::GreaterOrEqual, Version);
                AddUpperBound(LastComponent);
            }
            else
            {
                AddComparator(EComparison::Equal, Version);
            }
        }

        NormalizedTokens.Add(Operator + VersionText);
    }

    Result.Text = FString::Join(NormalizedTokens, TEXT(" "));
    Out = MoveTemp(Result);
    if (OutError)
    {
        OutError->Reset();
    }
    return true;
}

bool FUBotVersionConstraint::IsSatisfiedBy(const FUBotSemVer& Version) const
{
    for (const FComparator& Comparator : Comparators)
    {
        const int32 Order = Version.Compare(Comparator.Version);
        bool bHolds = false;
        switch (Comparator.Comparison)
        {
        case EComparison::Equal:
            bHolds = Order == 0;
            break;
        case EComparison::Greater:
            bHolds = Order > 0;
            break;
        case EComparison::GreaterOrEqual:
            bHolds = Order >= 0;
            break;
        case EComparison::Less:
            bHolds = Order < 0;
            break;
        case EComparison::LessOrEqual:
            bHolds = Order <= 0;
            break;
        }

        if (!bHolds)
        {
            return false;
        }
    }
    return true;
}

bool FUBotVersionConstraint::IsAny() const
{
    return Comparators.IsEmpty();
}

FString FUBotVersionConstraint::ToString() const
{
    return Text;
}
