#pragma once

#include "CoreMinimal.h"

/**
 * Semantic version (SemVer 2.0) of a uBot package. Parsing is lenient about the release part so
 * that plugin VersionName values such as "1" or "v1.2" are accepted; missing components are zero.
 */
struct UBOTCORE_API FUBotSemVer
{
    int32 Major = 0;
    int32 Minor = 0;
    int32 Patch = 0;

    /** Dot separated identifiers following '-', without the dash. Empty for a release version. */
    FString PreRelease;

    FUBotSemVer() = default;
    FUBotSemVer(int32 InMajor, int32 InMinor, int32 InPatch, const FString& InPreRelease = FString());

    /**
     * Accepts "1", "1.2", "1.2.3", an optional leading 'v', an optional "-prerelease" and ignores
     * "+build" metadata. Surrounding whitespace is ignored. Out is only written on success.
     */
    static bool Parse(const FString& Text, FUBotSemVer& Out);

    /** Returns -1, 0 or 1. Prerelease versions follow SemVer 2.0 precedence; build metadata is not stored. */
    int32 Compare(const FUBotSemVer& Other) const;

    bool IsPreRelease() const
    {
        return !PreRelease.IsEmpty();
    }

    /** "Major.Minor.Patch" plus "-PreRelease" when present. */
    FString ToString() const;

    bool operator==(const FUBotSemVer& Other) const { return Compare(Other) == 0; }
    bool operator!=(const FUBotSemVer& Other) const { return Compare(Other) != 0; }
    bool operator<(const FUBotSemVer& Other) const { return Compare(Other) < 0; }
    bool operator<=(const FUBotSemVer& Other) const { return Compare(Other) <= 0; }
    bool operator>(const FUBotSemVer& Other) const { return Compare(Other) > 0; }
    bool operator>=(const FUBotSemVer& Other) const { return Compare(Other) >= 0; }
};

/**
 * Version range used by uBot package requirements.
 *
 * Grammar: empty or "*" (any version) | one or more whitespace separated comparators that must all
 * hold. A comparator is [op]version with op in = >= > <= < ^ ~ (no op means =). An operator may
 * also be separated from its version by whitespace (">= 1.0").
 *
 *   ^1.2.3 := >=1.2.3 <2.0.0     ^0.2.3 := >=0.2.3 <0.3.0     ^0.0.3 := >=0.0.3 <0.0.4
 *   ~1.2.3 := >=1.2.3 <1.3.0     ~1.2   := >=1.2.0 <1.3.0     ~1     := >=1.0.0 <2.0.0
 *
 * Partial versions are X-ranges as in node-semver: "1.2" and "=1.2" mean >=1.2.0 <1.3.0, ">1.2"
 * means >=1.3.0, "<=1.2" means <1.3.0, "<1.2" means <1.2.0, ">=1.2" means >=1.2.0; "^0" means
 * <1.0.0 and "^0.0" means <0.1.0.
 *
 * Versions are ordered by SemVer precedence, so prerelease versions take part like any other.
 * The one exception: every upper bound produced by ^, ~ or a partial version excludes the
 * prereleases of that bound (it is really <X.Y.Z-0), so ^1.2.3 does not accept 2.0.0-beta.
 */
struct UBOTCORE_API FUBotVersionConstraint
{
    /** On failure Out is left untouched and OutError (if given) describes the problem. */
    static bool Parse(const FString& Text, FUBotVersionConstraint& Out, FString* OutError = nullptr);

    bool IsSatisfiedBy(const FUBotSemVer& Version) const;

    /** True when the constraint has no comparators ("" or "*"). */
    bool IsAny() const;

    /** The parsed text with whitespace normalised (single spaces, operators joined to their version). */
    FString ToString() const;

private:
    enum class EComparison : uint8
    {
        Equal,
        Greater,
        GreaterOrEqual,
        Less,
        LessOrEqual
    };

    struct FComparator
    {
        EComparison Comparison = EComparison::Equal;
        FUBotSemVer Version;
    };

    TArray<FComparator> Comparators;
    FString Text;
};
