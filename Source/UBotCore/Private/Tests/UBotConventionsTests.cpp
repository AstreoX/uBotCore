#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "UBotConventionsLibrary.h"
#include "UBotIdentityComponent.h"
#include "UBotRightHandedConventions.h"
#include "UBotRosConventions.h"
#include "UBotUnits.h"
#include "UObject/Class.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace UBotConventionsTests
{
    constexpr int32 RandomSeed = 20251009;
    constexpr int32 RandomIterations = 500;
    constexpr double Tolerance = 1.e-9;

    // Random draws are sequenced into locals because argument evaluation order is unspecified, and
    // the fixed seed should give the same values with every compiler.

    /** Uniformly distributed unit quaternion, including negative w and both signs of each rotation. */
    FQuat RandomRotation(FRandomStream& Stream)
    {
        FQuat Candidate = FQuat::Identity;
        double SizeSquared = 0.0;
        do
        {
            Candidate.X = Stream.FRandRange(-1.0, 1.0);
            Candidate.Y = Stream.FRandRange(-1.0, 1.0);
            Candidate.Z = Stream.FRandRange(-1.0, 1.0);
            Candidate.W = Stream.FRandRange(-1.0, 1.0);
            SizeSquared = Candidate.SizeSquared();
        }
        while (SizeSquared > 1.0 || SizeSquared < 0.01);
        return Candidate.GetNormalized();
    }

    FVector RandomVector(FRandomStream& Stream, double Extent)
    {
        FVector Result = FVector::ZeroVector;
        Result.X = Stream.FRandRange(-Extent, Extent);
        Result.Y = Stream.FRandRange(-Extent, Extent);
        Result.Z = Stream.FRandRange(-Extent, Extent);
        return Result;
    }

    FTransform RandomRigidTransform(FRandomStream& Stream, double TranslationExtent)
    {
        const FQuat Rotation = RandomRotation(Stream);
        const FVector Translation = RandomVector(Stream, TranslationExtent);
        return FTransform(Rotation, Translation);
    }

    double VectorError(const FVector& A, const FVector& B)
    {
        return (A - B).GetAbsMax();
    }

    /** Largest component difference between A and B, treating q and -q as the same rotation. */
    double QuatError(const FQuat& A, const FQuat& B)
    {
        const double Same = FMath::Max(FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y)), FMath::Max(FMath::Abs(A.Z - B.Z), FMath::Abs(A.W - B.W)));
        const double Opposite = FMath::Max(FMath::Max(FMath::Abs(A.X + B.X), FMath::Abs(A.Y + B.Y)), FMath::Max(FMath::Abs(A.Z + B.Z), FMath::Abs(A.W + B.W)));
        return FMath::Min(Same, Opposite);
    }

    /** Exact equality: the forwarding functions must not differ by a single ulp. */
    bool SameVector(const FVector& A, const FVector& B)
    {
        return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
    }

    bool SameQuat(const FQuat& A, const FQuat& B)
    {
        return A.X == B.X && A.Y == B.Y && A.Z == B.Z && A.W == B.W;
    }

    bool SameTransform(const FTransform& A, const FTransform& B)
    {
        return SameQuat(A.GetRotation(), B.GetRotation()) && SameVector(A.GetTranslation(), B.GetTranslation()) && SameVector(A.GetScale3D(), B.GetScale3D());
    }

    /** Random string over characters a sanitizer has to keep, replace or collapse. */
    FString RandomNameInput(FRandomStream& Stream)
    {
        const TCHAR Alphabet[] = TEXT("aZ09_ -./:~!\u00E9\u4E2D");
        const int32 AlphabetLength = UE_ARRAY_COUNT(Alphabet) - 1;
        FString Input;
        const int32 Length = Stream.RandRange(0, 12);
        for (int32 Index = 0; Index < Length; ++Index)
        {
            Input.AppendChar(Alphabet[Stream.RandRange(0, AlphabetLength - 1)]);
        }
        return Input;
    }

    /**
     * Mean angular velocity that turns From into To within DeltaSeconds along the shortest arc.
     * World frame: To = Delta * From. Body frame: To = From * Delta.
     */
    FVector AngularVelocityFromQuats(const FQuat& From, const FQuat& To, double DeltaSeconds, bool bBodyFrame)
    {
        FQuat Delta = bBodyFrame ? From.Inverse() * To : To * From.Inverse();
        Delta.Normalize();
        if (Delta.W < 0.0)
        {
            Delta = FQuat(-Delta.X, -Delta.Y, -Delta.Z, -Delta.W);
        }
        const FVector Imaginary(Delta.X, Delta.Y, Delta.Z);
        const double SinHalfAngle = Imaginary.Size();
        if (SinHalfAngle < UE_DOUBLE_SMALL_NUMBER)
        {
            return FVector::ZeroVector;
        }
        const double Angle = 2.0 * FMath::Atan2(SinHalfAngle, Delta.W);
        return Imaginary * (Angle / (SinHalfAngle * DeltaSeconds));
    }

    bool IsValidRosName(const FString& Name)
    {
        if (Name.IsEmpty() || (Name[0] >= TEXT('0') && Name[0] <= TEXT('9')))
        {
            return false;
        }
        for (int32 Index = 0; Index < Name.Len(); ++Index)
        {
            const TCHAR Character = Name[Index];
            const bool bAllowed = (Character >= TEXT('a') && Character <= TEXT('z'))
                || (Character >= TEXT('A') && Character <= TEXT('Z'))
                || (Character >= TEXT('0') && Character <= TEXT('9'))
                || Character == TEXT('_');
            if (!bAllowed || (Character == TEXT('_') && Index > 0 && Name[Index - 1] == TEXT('_')))
            {
                return false;
            }
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsUnitsTest, "UBotCore.Conventions.Units", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsUnitsTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    static_assert(UBot::Units::CentimetersToMeters(250.0) == 2.5, "Scalar unit helpers are constexpr");
    static_assert(UBot::Units::MetersToCentimeters(2.5) == 250.0, "Scalar unit helpers are constexpr");

    TestEqual(TEXT("Centimeters per meter"), UBot::Units::CentimetersPerMeter, 100.0);
    TestEqual(TEXT("Centimeters to meters"), UBot::Units::CentimetersToMeters(-12.5), -0.125, Tolerance);
    TestEqual(TEXT("Meters to centimeters"), UBot::Units::MetersToCentimeters(-1.25), -125.0, Tolerance);
    TestTrue(TEXT("Vector centimeters to meters"), VectorError(UBot::Units::CentimetersToMeters(FVector(100.0, -250.0, 50.0)), FVector(1.0, -2.5, 0.5)) <= Tolerance);
    TestTrue(TEXT("Vector meters to centimeters"), VectorError(UBot::Units::MetersToCentimeters(FVector(1.0, -2.5, 0.5)), FVector(100.0, -250.0, 50.0)) <= Tolerance);
    TestEqual(TEXT("180 degrees is pi radians"), UBot::Units::DegreesToRadians(180.0), UE_DOUBLE_PI, Tolerance);
    TestEqual(TEXT("pi/2 radians is 90 degrees"), UBot::Units::RadiansToDegrees(UE_DOUBLE_HALF_PI), 90.0, Tolerance);
    TestEqual(TEXT("Degrees round trip"), UBot::Units::RadiansToDegrees(UBot::Units::DegreesToRadians(-37.5)), -37.5, Tolerance);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsAxisMappingTest, "UBotCore.Conventions.AxisMapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsAxisMappingTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    TestTrue(TEXT("UE position cm -> right-handed meters with Y mirrored"), VectorError(UBot::RightHanded::PositionFromUE(FVector(100.0, 250.0, -50.0)), FVector(1.0, -2.5, -0.5)) <= Tolerance);
    TestTrue(TEXT("Right-handed position meters -> UE cm with Y mirrored"), VectorError(UBot::RightHanded::PositionToUE(FVector(1.0, -2.5, -0.5)), FVector(100.0, 250.0, -50.0)) <= Tolerance);
    TestTrue(TEXT("Polar vector UE -> right-handed"), VectorError(UBot::RightHanded::VectorFromUE(FVector(1.0, 2.0, 3.0)), FVector(1.0, -2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Polar vector right-handed -> UE"), VectorError(UBot::RightHanded::VectorToUE(FVector(1.0, -2.0, 3.0)), FVector(1.0, 2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Axial vector UE -> right-handed"), VectorError(UBot::RightHanded::AngularVelocityFromUE(FVector(1.0, 2.0, 3.0)), FVector(-1.0, 2.0, -3.0)) <= Tolerance);
    TestTrue(TEXT("Axial vector right-handed -> UE"), VectorError(UBot::RightHanded::AngularVelocityToUE(FVector(-1.0, 2.0, -3.0)), FVector(1.0, 2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Axial vector is the negated polar mapping"), VectorError(UBot::RightHanded::AngularVelocityFromUE(FVector(0.3, -0.7, 1.1)), -UBot::RightHanded::VectorFromUE(FVector(0.3, -0.7, 1.1))) <= Tolerance);

    const FQuat Unit = FQuat(0.1, 0.2, 0.3, 0.4).GetNormalized();
    const FQuat Mirrored = UBot::RightHanded::RotationFromUE(Unit);
    TestTrue(TEXT("Quaternion (x, y, z, w) -> (-x, y, -z, w)"), FMath::Abs(Mirrored.X + Unit.X) <= Tolerance && FMath::Abs(Mirrored.Y - Unit.Y) <= Tolerance
        && FMath::Abs(Mirrored.Z + Unit.Z) <= Tolerance && FMath::Abs(Mirrored.W - Unit.W) <= Tolerance);
    TestTrue(TEXT("Unnormalized input is normalized"), QuatError(UBot::RightHanded::RotationFromUE(FQuat(0.2, 0.4, 0.6, 0.8)), Mirrored) <= Tolerance);
    TestTrue(TEXT("Degenerate input yields identity"), QuatError(UBot::RightHanded::RotationFromUE(FQuat(0.0, 0.0, 0.0, 0.0)), FQuat::Identity) <= Tolerance);
    TestTrue(TEXT("Rotation right-handed -> UE uses the same mapping"), QuatError(UBot::RightHanded::RotationToUE(Unit), Mirrored) <= Tolerance);

    const FTransform Scaled(Unit, FVector(100.0, 200.0, 300.0), FVector(2.0, 3.0, 4.0));
    const FTransform MappedTransform = UBot::RightHanded::TransformFromUE(Scaled);
    TestTrue(TEXT("Transform scale is dropped"), VectorError(MappedTransform.GetScale3D(), FVector::OneVector) <= Tolerance);
    TestTrue(TEXT("Transform translation cm -> m"), VectorError(MappedTransform.GetTranslation(), FVector(1.0, -2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Transform rotation mirrored"), QuatError(MappedTransform.GetRotation(), Mirrored) <= Tolerance);
    TestTrue(TEXT("Right-handed -> UE transform scale is dropped"), VectorError(UBot::RightHanded::TransformToUE(FTransform(Unit, FVector::ZeroVector, FVector(5.0))).GetScale3D(), FVector::OneVector) <= Tolerance);

    TestEqual(TEXT("Yaw UE -> right-handed is negated"), UBot::RightHanded::YawFromUE(0.75), -0.75);
    TestEqual(TEXT("Yaw right-handed -> UE is negated"), UBot::RightHanded::YawToUE(-0.75), 0.75);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsRoundTripTest, "UBotCore.Conventions.RoundTrips", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    FRandomStream Stream(RandomSeed);
    double PositionError = 0.0;
    double PositionReverseError = 0.0;
    double VectorRoundTripError = 0.0;
    double AngularError = 0.0;
    double RotationError = 0.0;
    double TransformRotationError = 0.0;
    double TransformTranslationError = 0.0;
    double YawError = 0.0;
    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        const FVector Centimeters = RandomVector(Stream, 100000.0);
        const FVector Meters = RandomVector(Stream, 1000.0);
        const FVector Value = RandomVector(Stream, 50.0);
        const FQuat Rotation = RandomRotation(Stream);
        const FTransform Transform = RandomRigidTransform(Stream, 10000.0);
        const double Yaw = Stream.FRandRange(-UE_DOUBLE_PI, UE_DOUBLE_PI);

        PositionError = FMath::Max(PositionError, VectorError(UBot::RightHanded::PositionToUE(UBot::RightHanded::PositionFromUE(Centimeters)), Centimeters));
        PositionReverseError = FMath::Max(PositionReverseError, VectorError(UBot::RightHanded::PositionFromUE(UBot::RightHanded::PositionToUE(Meters)), Meters));
        VectorRoundTripError = FMath::Max(VectorRoundTripError, VectorError(UBot::RightHanded::VectorToUE(UBot::RightHanded::VectorFromUE(Value)), Value));
        VectorRoundTripError = FMath::Max(VectorRoundTripError, VectorError(UBot::RightHanded::VectorFromUE(UBot::RightHanded::VectorToUE(Value)), Value));
        AngularError = FMath::Max(AngularError, VectorError(UBot::RightHanded::AngularVelocityToUE(UBot::RightHanded::AngularVelocityFromUE(Value)), Value));
        AngularError = FMath::Max(AngularError, VectorError(UBot::RightHanded::AngularVelocityFromUE(UBot::RightHanded::AngularVelocityToUE(Value)), Value));
        RotationError = FMath::Max(RotationError, QuatError(UBot::RightHanded::RotationToUE(UBot::RightHanded::RotationFromUE(Rotation)), Rotation));
        RotationError = FMath::Max(RotationError, QuatError(UBot::RightHanded::RotationFromUE(UBot::RightHanded::RotationToUE(Rotation)), Rotation));

        const FTransform RoundTrip = UBot::RightHanded::TransformToUE(UBot::RightHanded::TransformFromUE(Transform));
        TransformRotationError = FMath::Max(TransformRotationError, QuatError(RoundTrip.GetRotation(), Transform.GetRotation()));
        TransformTranslationError = FMath::Max(TransformTranslationError, VectorError(RoundTrip.GetTranslation(), Transform.GetTranslation()));

        YawError = FMath::Max(YawError, FMath::Abs(UBot::RightHanded::YawToUE(UBot::RightHanded::YawFromUE(Yaw)) - Yaw));
        YawError = FMath::Max(YawError, FMath::Abs(UBot::RightHanded::YawFromUE(UBot::RightHanded::YawToUE(Yaw)) - Yaw));
    }

    TestTrue(*FString::Printf(TEXT("Position UE -> right-handed -> UE (max error %g cm)"), PositionError), PositionError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Position right-handed -> UE -> right-handed (max error %g m)"), PositionReverseError), PositionReverseError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Polar vector round trips (max error %g)"), VectorRoundTripError), VectorRoundTripError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Angular velocity round trips (max error %g)"), AngularError), AngularError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Rotation round trips (max error %g)"), RotationError), RotationError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Transform rotation round trip (max error %g)"), TransformRotationError), TransformRotationError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Transform translation round trip (max error %g cm)"), TransformTranslationError), TransformTranslationError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Yaw round trips (max error %g)"), YawError), YawError <= Tolerance);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsRotationMirrorTest, "UBotCore.Conventions.RotationMatchesVectorMirror", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsRotationMirrorTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    FRandomStream Stream(RandomSeed + 1);
    double RotateError = 0.0;
    double CompositionError = 0.0;
    double InverseError = 0.0;
    double NormError = 0.0;
    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        const FQuat A = RandomRotation(Stream);
        const FQuat B = RandomRotation(Stream);
        const FVector V = RandomVector(Stream, 500.0);

        // Rotating then mirroring equals mirroring then applying the mirrored rotation (R' = M R M).
        RotateError = FMath::Max(RotateError, VectorError(UBot::RightHanded::RotationFromUE(A).RotateVector(UBot::RightHanded::VectorFromUE(V)), UBot::RightHanded::VectorFromUE(A.RotateVector(V))));
        // The quaternion mapping is a homomorphism, so composed rotations and inverses map consistently.
        CompositionError = FMath::Max(CompositionError, QuatError(UBot::RightHanded::RotationFromUE(A * B), UBot::RightHanded::RotationFromUE(A) * UBot::RightHanded::RotationFromUE(B)));
        InverseError = FMath::Max(InverseError, QuatError(UBot::RightHanded::RotationFromUE(A.Inverse()), UBot::RightHanded::RotationFromUE(A).Inverse()));
        NormError = FMath::Max(NormError, FMath::Abs(UBot::RightHanded::RotationFromUE(A * 3.0).Size() - 1.0));
    }

    TestTrue(*FString::Printf(TEXT("RotationFromUE(q).RotateVector(VectorFromUE(v)) == VectorFromUE(q.RotateVector(v)) (max error %g)"), RotateError), RotateError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Mapping preserves composition (max error %g)"), CompositionError), CompositionError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Mapping preserves inverses (max error %g)"), InverseError), InverseError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Mapped rotations are normalized (max error %g)"), NormError), NormError <= Tolerance);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsAngularVelocityTest, "UBotCore.Conventions.AngularVelocityMatchesQuat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsAngularVelocityTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    constexpr double DeltaSeconds = 0.05;
    FRandomStream Stream(RandomSeed + 2);
    double WorldFrameError = 0.0;
    double BodyFrameError = 0.0;
    double PointVelocityError = 0.0;
    double TorqueError = 0.0;
    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        const FQuat Previous = RandomRotation(Stream);
        // Keep the step below pi so the shortest arc, and with it the derived velocity, is unique.
        const FVector StepAxis = Stream.GetUnitVector();
        const double StepAngle = Stream.FRandRange(-2.5, 2.5);
        const FQuat Step(StepAxis, StepAngle);
        const FQuat CurrentWorld = (Step * Previous).GetNormalized();
        const FQuat CurrentBody = (Previous * Step).GetNormalized();

        const FVector UEWorld = AngularVelocityFromQuats(Previous, CurrentWorld, DeltaSeconds, false);
        const FVector MappedWorld = AngularVelocityFromQuats(UBot::RightHanded::RotationFromUE(Previous), UBot::RightHanded::RotationFromUE(CurrentWorld), DeltaSeconds, false);
        WorldFrameError = FMath::Max(WorldFrameError, VectorError(UBot::RightHanded::AngularVelocityFromUE(UEWorld), MappedWorld));

        const FVector UEBody = AngularVelocityFromQuats(Previous, CurrentBody, DeltaSeconds, true);
        const FVector MappedBody = AngularVelocityFromQuats(UBot::RightHanded::RotationFromUE(Previous), UBot::RightHanded::RotationFromUE(CurrentBody), DeltaSeconds, true);
        BodyFrameError = FMath::Max(BodyFrameError, VectorError(UBot::RightHanded::AngularVelocityFromUE(UEBody), MappedBody));

        // Rigid body point velocity v = w x r must agree with the polar mapping of v.
        const FVector Lever = RandomVector(Stream, 5.0);
        PointVelocityError = FMath::Max(PointVelocityError, VectorError(
            FVector::CrossProduct(UBot::RightHanded::AngularVelocityFromUE(UEWorld), UBot::RightHanded::VectorFromUE(Lever)),
            UBot::RightHanded::VectorFromUE(FVector::CrossProduct(UEWorld, Lever))));

        // Torque t = r x F is axial as well.
        const FVector Force = RandomVector(Stream, 100.0);
        TorqueError = FMath::Max(TorqueError, VectorError(
            UBot::RightHanded::AngularVelocityFromUE(FVector::CrossProduct(Lever, Force)),
            FVector::CrossProduct(UBot::RightHanded::VectorFromUE(Lever), UBot::RightHanded::VectorFromUE(Force))));
    }

    // Velocities reach about 50 rad/s, so allow for the scale.
    TestTrue(*FString::Printf(TEXT("World-frame angular velocity from quaternions (max error %g rad/s)"), WorldFrameError), WorldFrameError <= 1.e-7);
    TestTrue(*FString::Printf(TEXT("Body-frame angular velocity from quaternions (max error %g rad/s)"), BodyFrameError), BodyFrameError <= 1.e-7);
    TestTrue(*FString::Printf(TEXT("w x r agrees with the polar mapping (max error %g)"), PointVelocityError), PointVelocityError <= 1.e-7);
    TestTrue(*FString::Printf(TEXT("r x F maps as an axial vector (max error %g)"), TorqueError), TorqueError <= 1.e-7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsTransformTest, "UBotCore.Conventions.TransformMatchesPoints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsTransformTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    FRandomStream Stream(RandomSeed + 3);
    double PointError = 0.0;
    double CompositionRotationError = 0.0;
    double CompositionTranslationError = 0.0;
    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        const FTransform A = RandomRigidTransform(Stream, 10000.0);
        const FTransform B = RandomRigidTransform(Stream, 10000.0);
        const FVector LocalCentimeters = RandomVector(Stream, 1000.0);

        // Mapping the transform and the local point gives the mapped world point.
        const FVector MappedWorld = UBot::RightHanded::TransformFromUE(A).TransformPosition(UBot::RightHanded::PositionFromUE(LocalCentimeters));
        PointError = FMath::Max(PointError, VectorError(MappedWorld, UBot::RightHanded::PositionFromUE(A.TransformPosition(LocalCentimeters))));

        // FTransform A * B applies A first, then B; the mapping keeps that structure.
        const FTransform MappedProduct = UBot::RightHanded::TransformFromUE(A * B);
        const FTransform ProductOfMapped = UBot::RightHanded::TransformFromUE(A) * UBot::RightHanded::TransformFromUE(B);
        CompositionRotationError = FMath::Max(CompositionRotationError, QuatError(MappedProduct.GetRotation(), ProductOfMapped.GetRotation()));
        CompositionTranslationError = FMath::Max(CompositionTranslationError, VectorError(MappedProduct.GetTranslation(), ProductOfMapped.GetTranslation()));
    }

    TestTrue(*FString::Printf(TEXT("Mapped transform moves mapped points (max error %g m)"), PointError), PointError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Mapping preserves transform composition, rotation (max error %g)"), CompositionRotationError), CompositionRotationError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Mapping preserves transform composition, translation (max error %g m)"), CompositionTranslationError), CompositionTranslationError <= Tolerance);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsYawTest, "UBotCore.Conventions.YawMatchesRotation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsYawTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    FRandomStream Stream(RandomSeed + 4);
    double HeadingError = 0.0;
    double RotatorError = 0.0;
    double BeamError = 0.0;
    double YawRateError = 0.0;
    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        const double UEYaw = Stream.FRandRange(-UE_DOUBLE_PI, UE_DOUBLE_PI);
        const double MappedYaw = UBot::RightHanded::YawFromUE(UEYaw);
        const FQuat UEHeading(FVector::UpVector, UEYaw);

        // A pure heading maps to a pure heading with the converted yaw.
        HeadingError = FMath::Max(HeadingError, QuatError(UBot::RightHanded::RotationFromUE(UEHeading), FQuat(FVector::UpVector, MappedYaw)));
        // FRotator yaw (degrees) describes the same rotation as the UE heading quaternion.
        RotatorError = FMath::Max(RotatorError, QuatError(FRotator(0.0, UBot::Units::RadiansToDegrees(UEYaw), 0.0).Quaternion(), UEHeading));
        // A planar scan beam at the UE angle points along the mapped angle after the vector mapping.
        const FVector UEBeam = UEHeading.RotateVector(FVector::ForwardVector);
        BeamError = FMath::Max(BeamError, VectorError(UBot::RightHanded::VectorFromUE(UEBeam), FVector(FMath::Cos(MappedYaw), FMath::Sin(MappedYaw), 0.0)));
        // A yaw rate is the Z component of the axial angular velocity.
        YawRateError = FMath::Max(YawRateError, FMath::Abs(UBot::RightHanded::AngularVelocityFromUE(FVector(0.0, 0.0, UEYaw)).Z - MappedYaw));
    }

    TestTrue(*FString::Printf(TEXT("Heading quaternion matches converted yaw (max error %g)"), HeadingError), HeadingError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("FRotator yaw matches the heading quaternion (max error %g)"), RotatorError), RotatorError <= 1.e-6);
    TestTrue(*FString::Printf(TEXT("Scan beam direction matches converted angle (max error %g)"), BeamError), BeamError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Yaw rate matches the angular velocity mapping (max error %g)"), YawRateError), YawRateError <= Tolerance);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsBlueprintNodesTest, "UBotCore.Conventions.BlueprintNodes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsBlueprintNodesTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    // The documented mappings through the nodes.
    TestTrue(TEXT("Position node: UE cm -> right-handed meters"), VectorError(UUBotConventionsLibrary::PositionUEToRightHanded(FVector(100.0, 250.0, -50.0)), FVector(1.0, -2.5, -0.5)) <= Tolerance);
    TestTrue(TEXT("Position node: right-handed meters -> UE cm"), VectorError(UUBotConventionsLibrary::PositionRightHandedToUE(FVector(1.0, -2.5, -0.5)), FVector(100.0, 250.0, -50.0)) <= Tolerance);
    TestTrue(TEXT("Vector node: UE -> right-handed"), VectorError(UUBotConventionsLibrary::VectorUEToRightHanded(FVector(1.0, 2.0, 3.0)), FVector(1.0, -2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Vector node: right-handed -> UE"), VectorError(UUBotConventionsLibrary::VectorRightHandedToUE(FVector(1.0, -2.0, 3.0)), FVector(1.0, 2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Angular velocity node: UE -> right-handed"), VectorError(UUBotConventionsLibrary::AngularVelocityUEToRightHanded(FVector(1.0, 2.0, 3.0)), FVector(-1.0, 2.0, -3.0)) <= Tolerance);
    TestTrue(TEXT("Angular velocity node: right-handed -> UE"), VectorError(UUBotConventionsLibrary::AngularVelocityRightHandedToUE(FVector(-1.0, 2.0, -3.0)), FVector(1.0, 2.0, 3.0)) <= Tolerance);

    const FQuat Unit = FQuat(0.1, 0.2, 0.3, 0.4).GetNormalized();
    const FQuat Mirrored = UUBotConventionsLibrary::RotationUEToRightHanded(Unit);
    TestTrue(TEXT("Rotation node: (x, y, z, w) -> (-x, y, -z, w)"), FMath::Abs(Mirrored.X + Unit.X) <= Tolerance && FMath::Abs(Mirrored.Y - Unit.Y) <= Tolerance
        && FMath::Abs(Mirrored.Z + Unit.Z) <= Tolerance && FMath::Abs(Mirrored.W - Unit.W) <= Tolerance);
    TestTrue(TEXT("Rotation node: right-handed -> UE uses the same mapping"), QuatError(UUBotConventionsLibrary::RotationRightHandedToUE(Unit), Mirrored) <= Tolerance);

    const FTransform Scaled(Unit, FVector(100.0, 200.0, 300.0), FVector(2.0, 3.0, 4.0));
    const FTransform MappedTransform = UUBotConventionsLibrary::TransformUEToRightHanded(Scaled);
    TestTrue(TEXT("Transform node: scale is dropped"), VectorError(MappedTransform.GetScale3D(), FVector::OneVector) <= Tolerance);
    TestTrue(TEXT("Transform node: translation cm -> m"), VectorError(MappedTransform.GetTranslation(), FVector(1.0, -2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Transform node: rotation mirrored"), QuatError(MappedTransform.GetRotation(), Mirrored) <= Tolerance);
    TestTrue(TEXT("Transform node: right-handed -> UE translation m -> cm"), VectorError(UUBotConventionsLibrary::TransformRightHandedToUE(MappedTransform).GetTranslation(), FVector(100.0, 200.0, 300.0)) <= Tolerance);

    TestEqual(TEXT("Yaw node: UE -> right-handed is negated"), UUBotConventionsLibrary::YawUEToRightHanded(0.75), -0.75);
    TestEqual(TEXT("Yaw node: right-handed -> UE is negated"), UUBotConventionsLibrary::YawRightHandedToUE(-0.75), 0.75);

    // The nodes are the C++ functions, not a second implementation.
    FRandomStream Stream(RandomSeed + 9);
    TSet<FString> Mismatched;
    const auto Check = [&Mismatched](const TCHAR* Name, bool bSame)
    {
        if (!bSame)
        {
            Mismatched.Add(FString(Name));
        }
    };
    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        const FVector Centimeters = RandomVector(Stream, 100000.0);
        const FVector Meters = RandomVector(Stream, 1000.0);
        const FVector Value = RandomVector(Stream, 50.0);
        const FQuat Rotation = RandomRotation(Stream);
        const FQuat TransformRotation = RandomRotation(Stream);
        const FVector TransformTranslation = RandomVector(Stream, 10000.0);
        const FVector TransformScale = FVector::OneVector + RandomVector(Stream, 0.5);
        const FTransform Transform(TransformRotation, TransformTranslation, TransformScale);
        const double Yaw = Stream.FRandRange(-UE_DOUBLE_PI, UE_DOUBLE_PI);

        Check(TEXT("PositionUEToRightHanded"), SameVector(UUBotConventionsLibrary::PositionUEToRightHanded(Centimeters), UBot::RightHanded::PositionFromUE(Centimeters)));
        Check(TEXT("PositionRightHandedToUE"), SameVector(UUBotConventionsLibrary::PositionRightHandedToUE(Meters), UBot::RightHanded::PositionToUE(Meters)));
        Check(TEXT("VectorUEToRightHanded"), SameVector(UUBotConventionsLibrary::VectorUEToRightHanded(Value), UBot::RightHanded::VectorFromUE(Value)));
        Check(TEXT("VectorRightHandedToUE"), SameVector(UUBotConventionsLibrary::VectorRightHandedToUE(Value), UBot::RightHanded::VectorToUE(Value)));
        Check(TEXT("AngularVelocityUEToRightHanded"), SameVector(UUBotConventionsLibrary::AngularVelocityUEToRightHanded(Value), UBot::RightHanded::AngularVelocityFromUE(Value)));
        Check(TEXT("AngularVelocityRightHandedToUE"), SameVector(UUBotConventionsLibrary::AngularVelocityRightHandedToUE(Value), UBot::RightHanded::AngularVelocityToUE(Value)));
        Check(TEXT("RotationUEToRightHanded"), SameQuat(UUBotConventionsLibrary::RotationUEToRightHanded(Rotation), UBot::RightHanded::RotationFromUE(Rotation)));
        Check(TEXT("RotationRightHandedToUE"), SameQuat(UUBotConventionsLibrary::RotationRightHandedToUE(Rotation), UBot::RightHanded::RotationToUE(Rotation)));
        Check(TEXT("TransformUEToRightHanded"), SameTransform(UUBotConventionsLibrary::TransformUEToRightHanded(Transform), UBot::RightHanded::TransformFromUE(Transform)));
        Check(TEXT("TransformRightHandedToUE"), SameTransform(UUBotConventionsLibrary::TransformRightHandedToUE(Transform), UBot::RightHanded::TransformToUE(Transform)));
        Check(TEXT("YawUEToRightHanded"), UUBotConventionsLibrary::YawUEToRightHanded(Yaw) == UBot::RightHanded::YawFromUE(Yaw));
        Check(TEXT("YawRightHandedToUE"), UUBotConventionsLibrary::YawRightHandedToUE(Yaw) == UBot::RightHanded::YawToUE(Yaw));
    }
    for (const FString& Name : Mismatched)
    {
        AddError(FString::Printf(TEXT("Node %s differs from the C++ function"), *Name));
    }
    TestEqual(TEXT("Nodes that differ from the C++ functions"), Mismatched.Num(), 0);

#if WITH_METADATA
    // Twelve new nodes; the seventeen old ones are deprecated with a message, none of the new ones is.
    const TCHAR* const NewNodes[] =
    {
        TEXT("PositionUEToRightHanded"), TEXT("PositionRightHandedToUE"), TEXT("VectorUEToRightHanded"), TEXT("VectorRightHandedToUE"),
        TEXT("AngularVelocityUEToRightHanded"), TEXT("AngularVelocityRightHandedToUE"), TEXT("RotationUEToRightHanded"), TEXT("RotationRightHandedToUE"),
        TEXT("TransformUEToRightHanded"), TEXT("TransformRightHandedToUE"), TEXT("YawUEToRightHanded"), TEXT("YawRightHandedToUE"),
    };
    const TCHAR* const DeprecatedNodes[] =
    {
        TEXT("PositionUEToROS"), TEXT("PositionROSToUE"), TEXT("VectorUEToROS"), TEXT("VectorROSToUE"),
        TEXT("AngularVelocityUEToROS"), TEXT("AngularVelocityROSToUE"), TEXT("RotationUEToROS"), TEXT("RotationROSToUE"),
        TEXT("TransformUEToROS"), TEXT("TransformROSToUE"), TEXT("YawUEToROS"), TEXT("YawROSToUE"),
        TEXT("CameraLinkToOpticalRotation"), TEXT("GetOpticalFrameSuffix"), TEXT("SecondsToRosTime"), TEXT("RosTimeToSeconds"), TEXT("SanitizeRosName"),
    };
    const UClass* LibraryClass = UUBotConventionsLibrary::StaticClass();
    for (const TCHAR* Name : NewNodes)
    {
        const UFunction* Function = LibraryClass->FindFunctionByName(Name);
        if (TestNotNull(*FString::Printf(TEXT("Node %s exists"), Name), Function))
        {
            TestFalse(*FString::Printf(TEXT("Node %s is not deprecated"), Name), Function->HasMetaData(TEXT("DeprecatedFunction")));
        }
    }
    for (const TCHAR* Name : DeprecatedNodes)
    {
        const UFunction* Function = LibraryClass->FindFunctionByName(Name);
        if (TestNotNull(*FString::Printf(TEXT("Node %s still exists"), Name), Function))
        {
            TestTrue(*FString::Printf(TEXT("Node %s is deprecated"), Name), Function->HasMetaData(TEXT("DeprecatedFunction")));
            TestFalse(*FString::Printf(TEXT("Node %s says what to use"), Name), Function->GetMetaData(TEXT("DeprecationMessage")).IsEmpty());
        }
    }
#endif // WITH_METADATA
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsDeprecatedForwardersTest, "UBotCore.Conventions.DeprecatedForwarders", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

PRAGMA_DISABLE_DEPRECATION_WARNINGS

bool FUBotConventionsDeprecatedForwardersTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    // The deprecated UBot::Ros math functions and the deprecated math nodes return exactly what the
    // UBot::RightHanded function returns, so migrating cannot change a result.
    FRandomStream Stream(RandomSeed + 8);
    TSet<FString> Mismatched;
    const auto Check = [&Mismatched](const TCHAR* Name, bool bSame)
    {
        if (!bSame)
        {
            Mismatched.Add(FString(Name));
        }
    };
    const auto CheckRotation = [&Check](const FQuat& Rotation)
    {
        Check(TEXT("UBot::Ros::RotationUEToROS"), SameQuat(UBot::Ros::RotationUEToROS(Rotation), UBot::RightHanded::RotationFromUE(Rotation)));
        Check(TEXT("UBot::Ros::RotationROSToUE"), SameQuat(UBot::Ros::RotationROSToUE(Rotation), UBot::RightHanded::RotationToUE(Rotation)));
        Check(TEXT("RotationUEToROS node"), SameQuat(UUBotConventionsLibrary::RotationUEToROS(Rotation), UBot::RightHanded::RotationFromUE(Rotation)));
        Check(TEXT("RotationROSToUE node"), SameQuat(UUBotConventionsLibrary::RotationROSToUE(Rotation), UBot::RightHanded::RotationToUE(Rotation)));
    };

    // Unnormalized and degenerate rotations take the same path through the forwarders.
    CheckRotation(FQuat(0.2, 0.4, 0.6, 0.8));
    CheckRotation(FQuat(0.0, 0.0, 0.0, 0.0));

    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        const FVector Centimeters = RandomVector(Stream, 100000.0);
        const FVector Meters = RandomVector(Stream, 1000.0);
        const FVector Value = RandomVector(Stream, 50.0);
        const FQuat Rotation = RandomRotation(Stream);
        const FQuat TransformRotation = RandomRotation(Stream);
        const FVector TransformTranslation = RandomVector(Stream, 10000.0);
        const FVector TransformScale = FVector::OneVector + RandomVector(Stream, 0.5);
        const FTransform Transform(TransformRotation, TransformTranslation, TransformScale);
        const double Yaw = Stream.FRandRange(-UE_DOUBLE_PI, UE_DOUBLE_PI);

        Check(TEXT("UBot::Ros::PositionUEToROS"), SameVector(UBot::Ros::PositionUEToROS(Centimeters), UBot::RightHanded::PositionFromUE(Centimeters)));
        Check(TEXT("UBot::Ros::PositionROSToUE"), SameVector(UBot::Ros::PositionROSToUE(Meters), UBot::RightHanded::PositionToUE(Meters)));
        Check(TEXT("UBot::Ros::VectorUEToROS"), SameVector(UBot::Ros::VectorUEToROS(Value), UBot::RightHanded::VectorFromUE(Value)));
        Check(TEXT("UBot::Ros::VectorROSToUE"), SameVector(UBot::Ros::VectorROSToUE(Value), UBot::RightHanded::VectorToUE(Value)));
        Check(TEXT("UBot::Ros::AngularVelocityUEToROS"), SameVector(UBot::Ros::AngularVelocityUEToROS(Value), UBot::RightHanded::AngularVelocityFromUE(Value)));
        Check(TEXT("UBot::Ros::AngularVelocityROSToUE"), SameVector(UBot::Ros::AngularVelocityROSToUE(Value), UBot::RightHanded::AngularVelocityToUE(Value)));
        Check(TEXT("UBot::Ros::TransformUEToROS"), SameTransform(UBot::Ros::TransformUEToROS(Transform), UBot::RightHanded::TransformFromUE(Transform)));
        Check(TEXT("UBot::Ros::TransformROSToUE"), SameTransform(UBot::Ros::TransformROSToUE(Transform), UBot::RightHanded::TransformToUE(Transform)));
        Check(TEXT("UBot::Ros::YawUEToROS"), UBot::Ros::YawUEToROS(Yaw) == UBot::RightHanded::YawFromUE(Yaw));
        Check(TEXT("UBot::Ros::YawROSToUE"), UBot::Ros::YawROSToUE(Yaw) == UBot::RightHanded::YawToUE(Yaw));
        CheckRotation(Rotation);

        Check(TEXT("PositionUEToROS node"), SameVector(UUBotConventionsLibrary::PositionUEToROS(Centimeters), UBot::RightHanded::PositionFromUE(Centimeters)));
        Check(TEXT("PositionROSToUE node"), SameVector(UUBotConventionsLibrary::PositionROSToUE(Meters), UBot::RightHanded::PositionToUE(Meters)));
        Check(TEXT("VectorUEToROS node"), SameVector(UUBotConventionsLibrary::VectorUEToROS(Value), UBot::RightHanded::VectorFromUE(Value)));
        Check(TEXT("VectorROSToUE node"), SameVector(UUBotConventionsLibrary::VectorROSToUE(Value), UBot::RightHanded::VectorToUE(Value)));
        Check(TEXT("AngularVelocityUEToROS node"), SameVector(UUBotConventionsLibrary::AngularVelocityUEToROS(Value), UBot::RightHanded::AngularVelocityFromUE(Value)));
        Check(TEXT("AngularVelocityROSToUE node"), SameVector(UUBotConventionsLibrary::AngularVelocityROSToUE(Value), UBot::RightHanded::AngularVelocityToUE(Value)));
        Check(TEXT("TransformUEToROS node"), SameTransform(UUBotConventionsLibrary::TransformUEToROS(Transform), UBot::RightHanded::TransformFromUE(Transform)));
        Check(TEXT("TransformROSToUE node"), SameTransform(UUBotConventionsLibrary::TransformROSToUE(Transform), UBot::RightHanded::TransformToUE(Transform)));
        Check(TEXT("YawUEToROS node"), UUBotConventionsLibrary::YawUEToROS(Yaw) == UBot::RightHanded::YawFromUE(Yaw));
        Check(TEXT("YawROSToUE node"), UUBotConventionsLibrary::YawROSToUE(Yaw) == UBot::RightHanded::YawToUE(Yaw));

        const FString Name = RandomNameInput(Stream);
        Check(TEXT("UBot::Ros::SanitizeRosName"), UBot::Ros::SanitizeRosName(Name).Equals(UBot::Identity::SanitizeNameToken(Name), ESearchCase::CaseSensitive));
        Check(TEXT("SanitizeRosName node"), UUBotConventionsLibrary::SanitizeRosName(Name).Equals(UBot::Identity::SanitizeNameToken(Name), ESearchCase::CaseSensitive));
    }
    TestTrue(TEXT("SanitizeRosName of an empty string"), UBot::Ros::SanitizeRosName(FString()).Equals(UBot::Identity::SanitizeNameToken(FString()), ESearchCase::CaseSensitive));

    for (const FString& Name : Mismatched)
    {
        AddError(FString::Printf(TEXT("%s differs from its replacement"), *Name));
    }
    TestEqual(TEXT("Deprecated forwarders that differ from their replacement"), Mismatched.Num(), 0);
    return true;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsCameraOpticalTest, "UBotCore.Conventions.CameraOpticalFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

PRAGMA_DISABLE_DEPRECATION_WARNINGS

bool FUBotConventionsCameraOpticalTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    const FQuat LinkToOptical = UBot::Ros::CameraLinkToOpticalRotation();
    TestTrue(TEXT("Optical rotation is normalized"), FMath::Abs(LinkToOptical.Size() - 1.0) <= Tolerance);
    TestTrue(TEXT("Optical rotation components"), FMath::Abs(LinkToOptical.X + 0.5) <= Tolerance && FMath::Abs(LinkToOptical.Y - 0.5) <= Tolerance
        && FMath::Abs(LinkToOptical.Z + 0.5) <= Tolerance && FMath::Abs(LinkToOptical.W - 0.5) <= Tolerance);
    TestTrue(TEXT("Optical +Z (forward) is link +X"), VectorError(LinkToOptical.RotateVector(FVector(0.0, 0.0, 1.0)), FVector(1.0, 0.0, 0.0)) <= Tolerance);
    TestTrue(TEXT("Optical +X (right) is link -Y"), VectorError(LinkToOptical.RotateVector(FVector(1.0, 0.0, 0.0)), FVector(0.0, -1.0, 0.0)) <= Tolerance);
    TestTrue(TEXT("Optical +Y (down) is link -Z"), VectorError(LinkToOptical.RotateVector(FVector(0.0, 1.0, 0.0)), FVector(0.0, 0.0, -1.0)) <= Tolerance);

    // End to end: a UE camera's forward, right and down directions are the ROS optical +Z, +X and +Y.
    FRandomStream Stream(RandomSeed + 5);
    double AxisError = 0.0;
    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        const FQuat CameraUE = RandomRotation(Stream);
        const FQuat OpticalROS = UBot::RightHanded::RotationFromUE(CameraUE) * LinkToOptical;
        AxisError = FMath::Max(AxisError, VectorError(OpticalROS.RotateVector(FVector::ZAxisVector), UBot::RightHanded::VectorFromUE(CameraUE.GetForwardVector())));
        AxisError = FMath::Max(AxisError, VectorError(OpticalROS.RotateVector(FVector::XAxisVector), UBot::RightHanded::VectorFromUE(CameraUE.GetRightVector())));
        AxisError = FMath::Max(AxisError, VectorError(OpticalROS.RotateVector(FVector::YAxisVector), UBot::RightHanded::VectorFromUE(-CameraUE.GetUpVector())));
    }
    TestTrue(*FString::Printf(TEXT("UE camera axes match the ROS optical frame (max error %g)"), AxisError), AxisError <= Tolerance);

    TestEqualSensitive(TEXT("Optical frame suffix"), UBot::Ros::OpticalFrameSuffix(), TEXT("_optical"));
    TestEqualSensitive(TEXT("Blueprint optical frame suffix"), UUBotConventionsLibrary::GetOpticalFrameSuffix(), TEXT("_optical"));
    return true;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsRosTimeTest, "UBotCore.Conventions.RosTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

PRAGMA_DISABLE_DEPRECATION_WARNINGS

bool FUBotConventionsRosTimeTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    const auto Check = [this](double Seconds, int32 ExpectedSec, uint32 ExpectedNanosec)
    {
        int32 Sec = -1;
        uint32 Nanosec = MAX_uint32;
        UBot::Ros::SecondsToRosTime(Seconds, Sec, Nanosec);
        TestEqual(*FString::Printf(TEXT("%.10f s -> sec"), Seconds), Sec, ExpectedSec);
        TestEqual(*FString::Printf(TEXT("%.10f s -> nanosec"), Seconds), static_cast<int64>(Nanosec), static_cast<int64>(ExpectedNanosec));
    };

    Check(0.0, 0, 0);
    Check(-1.0, 0, 0);
    Check(-1.e-12, 0, 0);
    Check(std::numeric_limits<double>::quiet_NaN(), 0, 0);
    Check(std::numeric_limits<double>::infinity(), 0, 0);
    Check(-std::numeric_limits<double>::infinity(), 0, 0);
    Check(0.9999999999, 1, 0);
    Check(41.9999999996, 42, 0);
    Check(1.5, 1, 500000000);
    Check(1.25, 1, 250000000);
    Check(2.000000001, 2, 1);
    Check(12345.678, 12345, 678000000);
    Check(2147483647.25, MAX_int32, 250000000);
    Check(3.0e9, MAX_int32, 999999999);

    for (int32 WholeSeconds = 1; WholeSeconds <= 100; ++WholeSeconds)
    {
        int32 Sec = 0;
        uint32 Nanosec = 0;
        UBot::Ros::SecondsToRosTime(static_cast<double>(WholeSeconds) - 1.e-11, Sec, Nanosec);
        if (Sec != WholeSeconds || Nanosec != 0)
        {
            AddError(FString::Printf(TEXT("%d s - 10 ps should carry to (%d, 0), got (%d, %u)"), WholeSeconds, WholeSeconds, Sec, Nanosec));
            break;
        }
    }

    TestEqual(TEXT("ROS time to seconds"), UBot::Ros::RosTimeToSeconds(1, 500000000), 1.5, 0.0);
    TestEqual(TEXT("ROS time zero"), UBot::Ros::RosTimeToSeconds(0, 0), 0.0, 0.0);
    TestEqual(TEXT("ROS time quarter"), UBot::Ros::RosTimeToSeconds(12, 250000000), 12.25, 0.0);

    FRandomStream Stream(RandomSeed + 6);
    int32 RoundTripFailures = 0;
    double MaxQuantizationError = 0.0;
    bool bNanosecondsInRange = true;
    for (int32 Iteration = 0; Iteration < 2000; ++Iteration)
    {
        const int32 Sec = Stream.RandRange(0, 1000000);
        const uint32 Milliseconds = static_cast<uint32>(Stream.RandRange(0, 999));
        const uint32 Nanosec = Milliseconds * 1000000u + static_cast<uint32>(Stream.RandRange(0, 999999));
        int32 OutSec = 0;
        uint32 OutNanosec = 0;
        UBot::Ros::SecondsToRosTime(UBot::Ros::RosTimeToSeconds(Sec, Nanosec), OutSec, OutNanosec);
        RoundTripFailures += (OutSec != Sec || OutNanosec != Nanosec) ? 1 : 0;

        const double Seconds = Stream.FRandRange(0.0, 1000000.0);
        UBot::Ros::SecondsToRosTime(Seconds, OutSec, OutNanosec);
        bNanosecondsInRange = bNanosecondsInRange && OutNanosec <= 999999999u;
        MaxQuantizationError = FMath::Max(MaxQuantizationError, FMath::Abs(UBot::Ros::RosTimeToSeconds(OutSec, OutNanosec) - Seconds));
    }
    TestEqual(TEXT("(sec, nanosec) -> seconds -> (sec, nanosec) round trip failures"), RoundTripFailures, 0);
    TestTrue(TEXT("Nanoseconds stay within [0, 999999999]"), bNanosecondsInRange);
    TestTrue(*FString::Printf(TEXT("Conversion error is at most half a nanosecond (max %g s)"), MaxQuantizationError), MaxQuantizationError <= 0.6e-9);

    int32 BlueprintSec = -1;
    int32 BlueprintNanosec = -1;
    UUBotConventionsLibrary::SecondsToRosTime(0.9999999999, BlueprintSec, BlueprintNanosec);
    TestTrue(TEXT("Blueprint seconds to ROS time carries"), BlueprintSec == 1 && BlueprintNanosec == 0);
    UUBotConventionsLibrary::SecondsToRosTime(7.125, BlueprintSec, BlueprintNanosec);
    TestTrue(TEXT("Blueprint seconds to ROS time"), BlueprintSec == 7 && BlueprintNanosec == 125000000);
    TestEqual(TEXT("Blueprint ROS time to seconds"), UUBotConventionsLibrary::RosTimeToSeconds(7, 125000000), 7.125, 0.0);
    TestEqual(TEXT("Blueprint negative nanoseconds count as zero"), UUBotConventionsLibrary::RosTimeToSeconds(7, -5), 7.0, 0.0);
    return true;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsSanitizeTest, "UBotCore.Conventions.SanitizeRosName", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

PRAGMA_DISABLE_DEPRECATION_WARNINGS

bool FUBotConventionsSanitizeTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsTests;

    struct FCase
    {
        const TCHAR* Input;
        const TCHAR* Expected;
    };
    const FCase Cases[] =
    {
        { TEXT(""), TEXT("ubot") },
        { TEXT("robot_1"), TEXT("robot_1") },
        { TEXT("BaseLink"), TEXT("BaseLink") },
        { TEXT("Robot A"), TEXT("Robot_A") },
        { TEXT("Rover-01"), TEXT("Rover_01") },
        { TEXT("/fleet/rover_a/"), TEXT("_fleet_rover_a_") },
        { TEXT("a--b"), TEXT("a_b") },
        { TEXT("a__b"), TEXT("a_b") },
        { TEXT("a - b"), TEXT("a_b") },
        { TEXT("robot.v2"), TEXT("robot_v2") },
        { TEXT("1robot"), TEXT("_1robot") },
        { TEXT("9"), TEXT("_9") },
        { TEXT("-1robot"), TEXT("_1robot") },
        { TEXT("_1robot"), TEXT("_1robot") },
        { TEXT("rob\u00F6t"), TEXT("rob_t") },
        { TEXT("\u673A\u5668\u4EBA_7"), TEXT("_7") },
        { TEXT("!!!"), TEXT("_") },
    };
    for (const FCase& Case : Cases)
    {
        TestEqualSensitive(*FString::Printf(TEXT("SanitizeRosName(\"%s\")"), Case.Input), UBot::Ros::SanitizeRosName(Case.Input), Case.Expected);
    }

    // Every output must be a valid ROS name token, whatever the input.
    const TCHAR Alphabet[] = TEXT("aZ09_ -./:~!\u00E9\u4E2D");
    const int32 AlphabetLength = UE_ARRAY_COUNT(Alphabet) - 1;
    FRandomStream Stream(RandomSeed + 7);
    int32 InvalidCount = 0;
    for (int32 Iteration = 0; Iteration < RandomIterations; ++Iteration)
    {
        FString Input;
        const int32 Length = Stream.RandRange(0, 12);
        for (int32 Index = 0; Index < Length; ++Index)
        {
            Input.AppendChar(Alphabet[Stream.RandRange(0, AlphabetLength - 1)]);
        }
        const FString Output = UBot::Ros::SanitizeRosName(Input);
        if (!IsValidRosName(Output))
        {
            ++InvalidCount;
            AddError(FString::Printf(TEXT("SanitizeRosName(\"%s\") produced invalid name \"%s\""), *Input, *Output));
        }
    }
    TestEqual(TEXT("Invalid sanitized names"), InvalidCount, 0);

    TestEqualSensitive(TEXT("Blueprint sanitize"), UUBotConventionsLibrary::SanitizeRosName(TEXT("Robot A")), TEXT("Robot_A"));
    return true;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

#endif // WITH_DEV_AUTOMATION_TESTS
