#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "UBotConventionsLibrary.h"
#include "UBotRosConventions.h"
#include "UBotUnits.h"

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

    TestTrue(TEXT("UE position cm -> ROS meters with Y mirrored"), VectorError(UBot::Ros::PositionUEToROS(FVector(100.0, 250.0, -50.0)), FVector(1.0, -2.5, -0.5)) <= Tolerance);
    TestTrue(TEXT("ROS position meters -> UE cm with Y mirrored"), VectorError(UBot::Ros::PositionROSToUE(FVector(1.0, -2.5, -0.5)), FVector(100.0, 250.0, -50.0)) <= Tolerance);
    TestTrue(TEXT("Polar vector UE -> ROS"), VectorError(UBot::Ros::VectorUEToROS(FVector(1.0, 2.0, 3.0)), FVector(1.0, -2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Polar vector ROS -> UE"), VectorError(UBot::Ros::VectorROSToUE(FVector(1.0, -2.0, 3.0)), FVector(1.0, 2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Axial vector UE -> ROS"), VectorError(UBot::Ros::AngularVelocityUEToROS(FVector(1.0, 2.0, 3.0)), FVector(-1.0, 2.0, -3.0)) <= Tolerance);
    TestTrue(TEXT("Axial vector ROS -> UE"), VectorError(UBot::Ros::AngularVelocityROSToUE(FVector(-1.0, 2.0, -3.0)), FVector(1.0, 2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Axial vector is the negated polar mapping"), VectorError(UBot::Ros::AngularVelocityUEToROS(FVector(0.3, -0.7, 1.1)), -UBot::Ros::VectorUEToROS(FVector(0.3, -0.7, 1.1))) <= Tolerance);

    const FQuat Unit = FQuat(0.1, 0.2, 0.3, 0.4).GetNormalized();
    const FQuat Mirrored = UBot::Ros::RotationUEToROS(Unit);
    TestTrue(TEXT("Quaternion (x, y, z, w) -> (-x, y, -z, w)"), FMath::Abs(Mirrored.X + Unit.X) <= Tolerance && FMath::Abs(Mirrored.Y - Unit.Y) <= Tolerance
        && FMath::Abs(Mirrored.Z + Unit.Z) <= Tolerance && FMath::Abs(Mirrored.W - Unit.W) <= Tolerance);
    TestTrue(TEXT("Unnormalized input is normalized"), QuatError(UBot::Ros::RotationUEToROS(FQuat(0.2, 0.4, 0.6, 0.8)), Mirrored) <= Tolerance);
    TestTrue(TEXT("Degenerate input yields identity"), QuatError(UBot::Ros::RotationUEToROS(FQuat(0.0, 0.0, 0.0, 0.0)), FQuat::Identity) <= Tolerance);
    TestTrue(TEXT("Rotation ROS -> UE uses the same mapping"), QuatError(UBot::Ros::RotationROSToUE(Unit), Mirrored) <= Tolerance);

    const FTransform Scaled(Unit, FVector(100.0, 200.0, 300.0), FVector(2.0, 3.0, 4.0));
    const FTransform ROSTransform = UBot::Ros::TransformUEToROS(Scaled);
    TestTrue(TEXT("Transform scale is dropped"), VectorError(ROSTransform.GetScale3D(), FVector::OneVector) <= Tolerance);
    TestTrue(TEXT("Transform translation cm -> m"), VectorError(ROSTransform.GetTranslation(), FVector(1.0, -2.0, 3.0)) <= Tolerance);
    TestTrue(TEXT("Transform rotation mirrored"), QuatError(ROSTransform.GetRotation(), Mirrored) <= Tolerance);
    TestTrue(TEXT("ROS -> UE transform scale is dropped"), VectorError(UBot::Ros::TransformROSToUE(FTransform(Unit, FVector::ZeroVector, FVector(5.0))).GetScale3D(), FVector::OneVector) <= Tolerance);

    TestEqual(TEXT("Yaw UE -> ROS is negated"), UBot::Ros::YawUEToROS(0.75), -0.75);
    TestEqual(TEXT("Yaw ROS -> UE is negated"), UBot::Ros::YawROSToUE(-0.75), 0.75);
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

        PositionError = FMath::Max(PositionError, VectorError(UBot::Ros::PositionROSToUE(UBot::Ros::PositionUEToROS(Centimeters)), Centimeters));
        PositionReverseError = FMath::Max(PositionReverseError, VectorError(UBot::Ros::PositionUEToROS(UBot::Ros::PositionROSToUE(Meters)), Meters));
        VectorRoundTripError = FMath::Max(VectorRoundTripError, VectorError(UBot::Ros::VectorROSToUE(UBot::Ros::VectorUEToROS(Value)), Value));
        VectorRoundTripError = FMath::Max(VectorRoundTripError, VectorError(UBot::Ros::VectorUEToROS(UBot::Ros::VectorROSToUE(Value)), Value));
        AngularError = FMath::Max(AngularError, VectorError(UBot::Ros::AngularVelocityROSToUE(UBot::Ros::AngularVelocityUEToROS(Value)), Value));
        AngularError = FMath::Max(AngularError, VectorError(UBot::Ros::AngularVelocityUEToROS(UBot::Ros::AngularVelocityROSToUE(Value)), Value));
        RotationError = FMath::Max(RotationError, QuatError(UBot::Ros::RotationROSToUE(UBot::Ros::RotationUEToROS(Rotation)), Rotation));
        RotationError = FMath::Max(RotationError, QuatError(UBot::Ros::RotationUEToROS(UBot::Ros::RotationROSToUE(Rotation)), Rotation));

        const FTransform RoundTrip = UBot::Ros::TransformROSToUE(UBot::Ros::TransformUEToROS(Transform));
        TransformRotationError = FMath::Max(TransformRotationError, QuatError(RoundTrip.GetRotation(), Transform.GetRotation()));
        TransformTranslationError = FMath::Max(TransformTranslationError, VectorError(RoundTrip.GetTranslation(), Transform.GetTranslation()));

        YawError = FMath::Max(YawError, FMath::Abs(UBot::Ros::YawROSToUE(UBot::Ros::YawUEToROS(Yaw)) - Yaw));
        YawError = FMath::Max(YawError, FMath::Abs(UBot::Ros::YawUEToROS(UBot::Ros::YawROSToUE(Yaw)) - Yaw));
    }

    TestTrue(*FString::Printf(TEXT("Position UE -> ROS -> UE (max error %g cm)"), PositionError), PositionError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Position ROS -> UE -> ROS (max error %g m)"), PositionReverseError), PositionReverseError <= Tolerance);
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
        RotateError = FMath::Max(RotateError, VectorError(UBot::Ros::RotationUEToROS(A).RotateVector(UBot::Ros::VectorUEToROS(V)), UBot::Ros::VectorUEToROS(A.RotateVector(V))));
        // The quaternion mapping is a homomorphism, so composed rotations and inverses map consistently.
        CompositionError = FMath::Max(CompositionError, QuatError(UBot::Ros::RotationUEToROS(A * B), UBot::Ros::RotationUEToROS(A) * UBot::Ros::RotationUEToROS(B)));
        InverseError = FMath::Max(InverseError, QuatError(UBot::Ros::RotationUEToROS(A.Inverse()), UBot::Ros::RotationUEToROS(A).Inverse()));
        NormError = FMath::Max(NormError, FMath::Abs(UBot::Ros::RotationUEToROS(A * 3.0).Size() - 1.0));
    }

    TestTrue(*FString::Printf(TEXT("RotationUEToROS(q).RotateVector(VectorUEToROS(v)) == VectorUEToROS(q.RotateVector(v)) (max error %g)"), RotateError), RotateError <= Tolerance);
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
        const FVector ROSWorld = AngularVelocityFromQuats(UBot::Ros::RotationUEToROS(Previous), UBot::Ros::RotationUEToROS(CurrentWorld), DeltaSeconds, false);
        WorldFrameError = FMath::Max(WorldFrameError, VectorError(UBot::Ros::AngularVelocityUEToROS(UEWorld), ROSWorld));

        const FVector UEBody = AngularVelocityFromQuats(Previous, CurrentBody, DeltaSeconds, true);
        const FVector ROSBody = AngularVelocityFromQuats(UBot::Ros::RotationUEToROS(Previous), UBot::Ros::RotationUEToROS(CurrentBody), DeltaSeconds, true);
        BodyFrameError = FMath::Max(BodyFrameError, VectorError(UBot::Ros::AngularVelocityUEToROS(UEBody), ROSBody));

        // Rigid body point velocity v = w x r must agree with the polar mapping of v.
        const FVector Lever = RandomVector(Stream, 5.0);
        PointVelocityError = FMath::Max(PointVelocityError, VectorError(
            FVector::CrossProduct(UBot::Ros::AngularVelocityUEToROS(UEWorld), UBot::Ros::VectorUEToROS(Lever)),
            UBot::Ros::VectorUEToROS(FVector::CrossProduct(UEWorld, Lever))));

        // Torque t = r x F is axial as well.
        const FVector Force = RandomVector(Stream, 100.0);
        TorqueError = FMath::Max(TorqueError, VectorError(
            UBot::Ros::AngularVelocityUEToROS(FVector::CrossProduct(Lever, Force)),
            FVector::CrossProduct(UBot::Ros::VectorUEToROS(Lever), UBot::Ros::VectorUEToROS(Force))));
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
        const FVector ROSWorld = UBot::Ros::TransformUEToROS(A).TransformPosition(UBot::Ros::PositionUEToROS(LocalCentimeters));
        PointError = FMath::Max(PointError, VectorError(ROSWorld, UBot::Ros::PositionUEToROS(A.TransformPosition(LocalCentimeters))));

        // FTransform A * B applies A first, then B; the mapping keeps that structure.
        const FTransform MappedProduct = UBot::Ros::TransformUEToROS(A * B);
        const FTransform ProductOfMapped = UBot::Ros::TransformUEToROS(A) * UBot::Ros::TransformUEToROS(B);
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
        const double ROSYaw = UBot::Ros::YawUEToROS(UEYaw);
        const FQuat UEHeading(FVector::UpVector, UEYaw);

        // A pure heading maps to a pure heading with the converted yaw.
        HeadingError = FMath::Max(HeadingError, QuatError(UBot::Ros::RotationUEToROS(UEHeading), FQuat(FVector::UpVector, ROSYaw)));
        // FRotator yaw (degrees) describes the same rotation as the UE heading quaternion.
        RotatorError = FMath::Max(RotatorError, QuatError(FRotator(0.0, UBot::Units::RadiansToDegrees(UEYaw), 0.0).Quaternion(), UEHeading));
        // A planar scan beam at the UE angle points along the ROS angle after the vector mapping.
        const FVector UEBeam = UEHeading.RotateVector(FVector::ForwardVector);
        BeamError = FMath::Max(BeamError, VectorError(UBot::Ros::VectorUEToROS(UEBeam), FVector(FMath::Cos(ROSYaw), FMath::Sin(ROSYaw), 0.0)));
        // A yaw rate is the Z component of the axial angular velocity.
        YawRateError = FMath::Max(YawRateError, FMath::Abs(UBot::Ros::AngularVelocityUEToROS(FVector(0.0, 0.0, UEYaw)).Z - ROSYaw));
    }

    TestTrue(*FString::Printf(TEXT("Heading quaternion matches converted yaw (max error %g)"), HeadingError), HeadingError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("FRotator yaw matches the heading quaternion (max error %g)"), RotatorError), RotatorError <= 1.e-6);
    TestTrue(*FString::Printf(TEXT("Scan beam direction matches converted angle (max error %g)"), BeamError), BeamError <= Tolerance);
    TestTrue(*FString::Printf(TEXT("Yaw rate matches the angular velocity mapping (max error %g)"), YawRateError), YawRateError <= Tolerance);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsCameraOpticalTest, "UBotCore.Conventions.CameraOpticalFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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
        const FQuat OpticalROS = UBot::Ros::RotationUEToROS(CameraUE) * LinkToOptical;
        AxisError = FMath::Max(AxisError, VectorError(OpticalROS.RotateVector(FVector::ZAxisVector), UBot::Ros::VectorUEToROS(CameraUE.GetForwardVector())));
        AxisError = FMath::Max(AxisError, VectorError(OpticalROS.RotateVector(FVector::XAxisVector), UBot::Ros::VectorUEToROS(CameraUE.GetRightVector())));
        AxisError = FMath::Max(AxisError, VectorError(OpticalROS.RotateVector(FVector::YAxisVector), UBot::Ros::VectorUEToROS(-CameraUE.GetUpVector())));
    }
    TestTrue(*FString::Printf(TEXT("UE camera axes match the ROS optical frame (max error %g)"), AxisError), AxisError <= Tolerance);

    TestEqualSensitive(TEXT("Optical frame suffix"), UBot::Ros::OpticalFrameSuffix(), TEXT("_optical"));
    TestEqualSensitive(TEXT("Blueprint optical frame suffix"), UUBotConventionsLibrary::GetOpticalFrameSuffix(), TEXT("_optical"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsRosTimeTest, "UBotCore.Conventions.RosTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsSanitizeTest, "UBotCore.Conventions.SanitizeRosName", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

#endif // WITH_DEV_AUTOMATION_TESTS
