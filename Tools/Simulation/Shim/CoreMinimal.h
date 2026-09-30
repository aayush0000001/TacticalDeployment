// Copyright TacticalDeployment. All Rights Reserved.
//
// Minimal stand-in for the slice of Unreal's Core/CoreUObject that the engine-light game rules
// use. It exists so Tools/Simulation can compile the *real* project headers and sources on a
// plain C++ toolchain. Semantics mirror UE where the game code depends on them (vector math,
// FRotator conventions, FMath helpers, FRandomStream::VRandCone). It is not a general UE shim.

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// --- Basic types / macros ------------------------------------------------------------------

using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using TCHAR = char;

#define TEXT(x) x
#define FORCEINLINE inline
#define TACTICALDEPLOYMENT_API
#define INDEX_NONE (-1)

#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define UINTERFACE(...)
#define UMETA(...)
#define GENERATED_BODY()

#define check(Expr) assert(Expr)
#define checkf(Expr, ...) assert(Expr)
#define checkSlow(Expr) assert(Expr)

struct FLogCategoryShim {};
#define DECLARE_LOG_CATEGORY_EXTERN(Name, Verbosity, CompileVerbosity) extern FLogCategoryShim Name
#define DEFINE_LOG_CATEGORY(Name) FLogCategoryShim Name
#define UE_LOG(...) do {} while (0)
#define UE_CLOG(...) do {} while (0)
#define DECLARE_STATS_GROUP(...)
#define DECLARE_CYCLE_STAT(...)
#define SCOPE_CYCLE_COUNTER(...)
#define LOCTEXT(Key, Text) FText()

#ifndef WITH_EDITOR
#define WITH_EDITOR 0
#endif
#ifndef UE_WITH_IRIS
#define UE_WITH_IRIS 0
#endif

inline constexpr float UE_PI = 3.14159265358979323846f;
inline constexpr float KINDA_SMALL_NUMBER = 1.e-4f;
inline constexpr float SMALL_NUMBER = 1.e-8f;
inline constexpr double UE_DOUBLE_SMALL_NUMBER = 1.e-8;
inline constexpr float UE_KINDA_SMALL_NUMBER = KINDA_SMALL_NUMBER;

template<typename T>
struct TNumericLimits
{
	static constexpr T Max() { return std::numeric_limits<T>::max(); }
	static constexpr T Min() { return std::numeric_limits<T>::min(); }
	static constexpr T Lowest() { return std::numeric_limits<T>::lowest(); }
};

struct FMemory
{
	static void* Memcpy(void* Dest, const void* Src, size_t Count) { return std::memcpy(Dest, Src, Count); }
	static void* Memzero(void* Dest, size_t Count) { return std::memset(Dest, 0, Count); }
	static void* Memset(void* Dest, uint8 Value, size_t Count) { return std::memset(Dest, Value, Count); }
};

// --- Containers ----------------------------------------------------------------------------

struct FDefaultAllocator {};
template<int32 N> struct TInlineAllocator {};
template<int32 N> struct TFixedAllocator {};

template<typename T, typename Allocator = FDefaultAllocator>
class TArray
{
public:
	TArray() = default;
	TArray(std::initializer_list<T> Init) : Data(Init) {}

	int32 Num() const { return static_cast<int32>(Data.size()); }
	bool IsValidIndex(int32 Index) const { return Index >= 0 && Index < Num(); }
	T& operator[](int32 Index) { assert(IsValidIndex(Index)); return Data[Index]; }
	const T& operator[](int32 Index) const { assert(IsValidIndex(Index)); return Data[Index]; }
	T& Last() { return Data.back(); }
	const T& Last() const { return Data.back(); }
	int32 Add(const T& Item) { Data.push_back(Item); return Num() - 1; }
	int32 AddUnique(const T& Item) { for (int32 i = 0; i < Num(); ++i) { if (Data[i] == Item) { return i; } } return Add(Item); }
	bool Contains(const T& Item) const { return std::find(Data.begin(), Data.end(), Item) != Data.end(); }
	void Reset() { Data.clear(); }
	void Empty() { Data.clear(); }
	void Init(const T& Value, int32 Count) { Data.assign(Count, Value); }
	void SetNum(int32 Count) { Data.resize(Count); }
	void SetNumZeroed(int32 Count)
	{
		static_assert(std::is_trivially_copyable_v<T>, "SetNumZeroed shim requires trivially copyable T");
		Data.resize(Count);
		if (Count > 0) { std::memset(static_cast<void*>(Data.data()), 0, sizeof(T) * Count); }
	}
	T* GetData() { return Data.data(); }
	auto begin() { return Data.begin(); }
	auto end() { return Data.end(); }
	auto begin() const { return Data.begin(); }
	auto end() const { return Data.end(); }

private:
	std::vector<T> Data;
};

// --- Strings / names (only what the game rules touch) --------------------------------------

class FName
{
public:
	FName() = default;
	FName(const char* In) : Value(In ? In : "") {}
	const std::string& ToString() const { return Value; }
	bool operator==(const FName& Other) const { return Value == Other.Value; }
private:
	std::string Value;
};

class FText {};

// --- FMath ----------------------------------------------------------------------------------

namespace UE::Math
{
	template<typename T> struct TVector;
	template<typename T> struct TVector2;
}

struct FMath
{
	template<typename T> static constexpr T Max(T A, T B) { return A > B ? A : B; }
	template<typename T> static constexpr T Min(T A, T B) { return A < B ? A : B; }
	template<typename T> static constexpr T Clamp(T X, T Lo, T Hi) { return X < Lo ? Lo : (X > Hi ? Hi : X); }
	// UE resolves mixed float/double/int arguments by promoting (RESOLVE_FLOAT_AMBIGUITY_*_ARGS).
	template<typename A, typename B, typename = std::enable_if_t<!std::is_same_v<A, B> && std::is_arithmetic_v<A> && std::is_arithmetic_v<B>>>
	static constexpr auto Max(A X, B Y) { using C = std::common_type_t<A, B>; return Max<C>(C(X), C(Y)); }
	template<typename A, typename B, typename = std::enable_if_t<!std::is_same_v<A, B> && std::is_arithmetic_v<A> && std::is_arithmetic_v<B>>>
	static constexpr auto Min(A X, B Y) { using C = std::common_type_t<A, B>; return Min<C>(C(X), C(Y)); }
	template<typename A, typename B, typename C2, typename = std::enable_if_t<!(std::is_same_v<A, B> && std::is_same_v<B, C2>) && std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && std::is_arithmetic_v<C2>>>
	static constexpr auto Clamp(A X, B Lo, C2 Hi) { using C = std::common_type_t<A, B, C2>; return Clamp<C>(C(X), C(Lo), C(Hi)); }
	template<typename T> static constexpr T Abs(T X) { return X < T(0) ? -X : X; }
	template<typename T> static constexpr T Square(T X) { return X * X; }
	static float Sqrt(float X) { return std::sqrt(X); }
	static double Sqrt(double X) { return std::sqrt(X); }
	static float Sin(float X) { return std::sin(X); }
	static float Cos(float X) { return std::cos(X); }
	static float Acos(float X) { return std::acos(Clamp(X, -1.f, 1.f)); }
	static float Atan2(float Y, float X) { return std::atan2(Y, X); }
	static float Exp(float X) { return std::exp(X); }
	static float Fmod(float X, float Y) { return std::fmod(X, Y); }
	static float DegreesToRadians(float Deg) { return Deg * (UE_PI / 180.f); }
	static float RadiansToDegrees(float Rad) { return Rad * (180.f / UE_PI); }
	static int32 RoundToInt(float X) { return static_cast<int32>(std::floor(X + 0.5f)); }
	static float RoundToFloat(float X) { return std::floor(X + 0.5f); }
	static int32 FloorToInt(float X) { return static_cast<int32>(std::floor(X)); }
	static int32 FloorToInt32(float X) { return static_cast<int32>(std::floor(X)); }

	template<typename T, typename U>
	static T Lerp(const T& A, const T& B, const U& Alpha) { return static_cast<T>(A + (B - A) * Alpha); }

	/** Same as UE: 0 below A, 1 at/above B, Hermite in between. */
	static float SmoothStep(float A, float B, float X)
	{
		if (X < A) { return 0.f; }
		if (X >= B) { return 1.f; }
		const float T = (X - A) / (B - A);
		return T * T * (3.f - 2.f * T);
	}

	template<typename T>
	static T GetMappedRangeValueClamped(const UE::Math::TVector2<T>& InRange, const UE::Math::TVector2<T>& OutRange, T Value);

	/** UE semantics (same-type arguments only, like the engine template). */
	template<typename T>
	static T ClampAngle(T AngleDegrees, T MinAngleDegrees, T MaxAngleDegrees);

	static double PointDistToSegmentSquared(const UE::Math::TVector<double>& Point, const UE::Math::TVector<double>& Start, const UE::Math::TVector<double>& End);
};

// --- Vectors ---------------------------------------------------------------------------------

namespace UE::Math
{
	template<typename T>
	struct TVector
	{
		T X = 0, Y = 0, Z = 0;

		static const TVector ZeroVector;
		static const TVector UpVector;
		static const TVector ForwardVector;
		static const TVector RightVector;

		constexpr TVector() = default;
		constexpr explicit TVector(T In) : X(In), Y(In), Z(In) {}
		constexpr TVector(T InX, T InY, T InZ) : X(InX), Y(InY), Z(InZ) {}
		template<typename U, typename = std::enable_if_t<!std::is_same_v<T, U>>>
		constexpr explicit TVector(const TVector<U>& Other) : X(T(Other.X)), Y(T(Other.Y)), Z(T(Other.Z)) {}

		TVector operator+(const TVector& V) const { return { X + V.X, Y + V.Y, Z + V.Z }; }
		TVector operator-(const TVector& V) const { return { X - V.X, Y - V.Y, Z - V.Z }; }
		TVector operator-() const { return { -X, -Y, -Z }; }
		template<typename S, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
		TVector operator*(S Scale) const { return { T(X * Scale), T(Y * Scale), T(Z * Scale) }; }
		template<typename S, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
		TVector operator/(S Scale) const { return { T(X / Scale), T(Y / Scale), T(Z / Scale) }; }
		TVector& operator+=(const TVector& V) { X += V.X; Y += V.Y; Z += V.Z; return *this; }
		TVector& operator-=(const TVector& V) { X -= V.X; Y -= V.Y; Z -= V.Z; return *this; }
		T operator|(const TVector& V) const { return X * V.X + Y * V.Y + Z * V.Z; }
		TVector operator^(const TVector& V) const { return { Y * V.Z - Z * V.Y, Z * V.X - X * V.Z, X * V.Y - Y * V.X }; }
		bool operator==(const TVector& V) const { return X == V.X && Y == V.Y && Z == V.Z; }

		T SizeSquared() const { return X * X + Y * Y + Z * Z; }
		T Size() const { return std::sqrt(SizeSquared()); }
		T Size2D() const { return std::sqrt(X * X + Y * Y); }
		bool IsZero() const { return X == 0 && Y == 0 && Z == 0; }
		bool IsNearlyZero(T Tolerance = T(KINDA_SMALL_NUMBER)) const { return std::abs(X) <= Tolerance && std::abs(Y) <= Tolerance && std::abs(Z) <= Tolerance; }
		TVector GetSafeNormal(T Tolerance = T(SMALL_NUMBER)) const
		{
			const T Sq = SizeSquared();
			return Sq <= Tolerance ? TVector() : *this / std::sqrt(Sq);
		}
		TVector GetSafeNormal2D(T Tolerance = T(SMALL_NUMBER)) const
		{
			const T Sq = X * X + Y * Y;
			return Sq <= Tolerance ? TVector() : TVector(X, Y, 0) / std::sqrt(Sq);
		}
		TVector ComponentMin(const TVector& V) const { return { std::min(X, V.X), std::min(Y, V.Y), std::min(Z, V.Z) }; }
		TVector ComponentMax(const TVector& V) const { return { std::max(X, V.X), std::max(Y, V.Y), std::max(Z, V.Z) }; }

		static T Dist(const TVector& A, const TVector& B) { return (A - B).Size(); }
		static T DistSquared(const TVector& A, const TVector& B) { return (A - B).SizeSquared(); }
		static T DistSquared2D(const TVector& A, const TVector& B) { const T Dx = A.X - B.X, Dy = A.Y - B.Y; return Dx * Dx + Dy * Dy; }
		static T DotProduct(const TVector& A, const TVector& B) { return A | B; }
		static TVector CrossProduct(const TVector& A, const TVector& B) { return A ^ B; }
	};

	template<typename T> inline const TVector<T> TVector<T>::ZeroVector(0, 0, 0);
	template<typename T> inline const TVector<T> TVector<T>::UpVector(0, 0, 1);
	template<typename T> inline const TVector<T> TVector<T>::ForwardVector(1, 0, 0);
	template<typename T> inline const TVector<T> TVector<T>::RightVector(0, 1, 0);

	template<typename S, typename T, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
	inline TVector<T> operator*(S Scale, const TVector<T>& V) { return V * Scale; }

	template<typename T>
	struct TVector2
	{
		T X = 0, Y = 0;
		constexpr TVector2() = default;
		constexpr TVector2(T InX, T InY) : X(InX), Y(InY) {}
		static const TVector2 ZeroVector;
		bool operator==(const TVector2& V) const { return X == V.X && Y == V.Y; }
		TVector2 operator-(const TVector2& V) const { return { X - V.X, Y - V.Y }; }
	};
	template<typename T> inline const TVector2<T> TVector2<T>::ZeroVector(0, 0);
}

using FVector = UE::Math::TVector<double>;
using FVector3f = UE::Math::TVector<float>;
using FVector3d = UE::Math::TVector<double>;
using FVector2D = UE::Math::TVector2<double>;
using FVector2f = UE::Math::TVector2<float>;

template<typename T>
T FMath::GetMappedRangeValueClamped(const UE::Math::TVector2<T>& InRange, const UE::Math::TVector2<T>& OutRange, T Value)
{
	const T Divisor = InRange.Y - InRange.X;
	const T Pct = Divisor == T(0) ? T(1) : Clamp((Value - InRange.X) / Divisor, T(0), T(1));
	return OutRange.X + (OutRange.Y - OutRange.X) * Pct;
}

inline double FMath::PointDistToSegmentSquared(const FVector& Point, const FVector& Start, const FVector& End)
{
	const FVector Segment = End - Start;
	const double LenSq = Segment.SizeSquared();
	const double T = LenSq > 0.0 ? Clamp(((Point - Start) | Segment) / LenSq, 0.0, 1.0) : 0.0;
	return (Point - (Start + Segment * T)).SizeSquared();
}

// --- Rotators / quaternions ------------------------------------------------------------------

namespace UE::Math
{
	template<typename T>
	struct TRotator
	{
		T Pitch = 0, Yaw = 0, Roll = 0;
		static const TRotator ZeroRotator;

		constexpr TRotator() = default;
		constexpr TRotator(T InPitch, T InYaw, T InRoll) : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}

		/** UE: into [0, 360). */
		static T ClampAxis(T Angle)
		{
			Angle = std::fmod(Angle, T(360));
			if (Angle < 0) { Angle += T(360); }
			return Angle;
		}

		/** UE: normalizes into (-180, 180]. */
		static T NormalizeAxis(T Angle)
		{
			Angle = ClampAxis(Angle);
			if (Angle > T(180)) { Angle -= T(360); }
			return Angle;
		}

		/** UE convention: X forward, Y right, Z up; +Pitch looks up, +Yaw turns right. */
		TVector<T> Vector() const
		{
			const T P = Pitch * T(UE_PI) / T(180), Yw = Yaw * T(UE_PI) / T(180);
			return { std::cos(P) * std::cos(Yw), std::cos(P) * std::sin(Yw), std::sin(P) };
		}

		TRotator operator+(const TRotator& R) const { return { Pitch + R.Pitch, Yaw + R.Yaw, Roll + R.Roll }; }
		TRotator operator-(const TRotator& R) const { return { Pitch - R.Pitch, Yaw - R.Yaw, Roll - R.Roll }; }
		template<typename S> TRotator operator*(S Scale) const { return { T(Pitch * Scale), T(Yaw * Scale), T(Roll * Scale) }; }
		TRotator GetNormalized() const { return { NormalizeAxis(Pitch), NormalizeAxis(Yaw), NormalizeAxis(Roll) }; }
	};
	template<typename T> inline const TRotator<T> TRotator<T>::ZeroRotator(0, 0, 0);

	template<typename T>
	struct TQuat
	{
		T X = 0, Y = 0, Z = 0, W = 1;
		static const TQuat Identity;

		constexpr TQuat() = default;
		constexpr TQuat(T InX, T InY, T InZ, T InW) : X(InX), Y(InY), Z(InZ), W(InW) {}
		TQuat(const TVector<T>& Axis, T AngleRad)
		{
			const T S = std::sin(AngleRad * T(0.5));
			X = Axis.X * S; Y = Axis.Y * S; Z = Axis.Z * S; W = std::cos(AngleRad * T(0.5));
		}

		TVector<T> RotateVector(const TVector<T>& V) const
		{
			const TVector<T> Q(X, Y, Z);
			const TVector<T> Tv = (Q ^ V) * T(2);
			return V + Tv * W + (Q ^ Tv);
		}
		TVector<T> GetAxisX() const { return RotateVector({ 1, 0, 0 }); }
		TVector<T> GetAxisY() const { return RotateVector({ 0, 1, 0 }); }
		TVector<T> GetAxisZ() const { return RotateVector({ 0, 0, 1 }); }

		TQuat operator*(const TQuat& Q) const
		{
			return { W * Q.X + X * Q.W + Y * Q.Z - Z * Q.Y,
			         W * Q.Y - X * Q.Z + Y * Q.W + Z * Q.X,
			         W * Q.Z + X * Q.Y - Y * Q.X + Z * Q.W,
			         W * Q.W - X * Q.X - Y * Q.Y - Z * Q.Z };
		}

		TQuat GetNormalized() const
		{
			const T SizeSq = X * X + Y * Y + Z * Z + W * W;
			if (SizeSq <= T(SMALL_NUMBER)) { return Identity; }
			const T Inv = T(1) / std::sqrt(SizeSq);
			return { X * Inv, Y * Inv, Z * Inv, W * Inv };
		}

		/** UE: linear blend with hemisphere correction; not normalized. */
		static TQuat FastLerp(const TQuat& A, const TQuat& B, T Alpha)
		{
			const T Dot = A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
			const T Bias = Dot >= T(0) ? T(1) : T(-1);
			return { B.X * Alpha + A.X * (Bias * (T(1) - Alpha)), B.Y * Alpha + A.Y * (Bias * (T(1) - Alpha)),
			         B.Z * Alpha + A.Z * (Bias * (T(1) - Alpha)), B.W * Alpha + A.W * (Bias * (T(1) - Alpha)) };
		}
	};
	template<typename T> inline const TQuat<T> TQuat<T>::Identity(0, 0, 0, 1);
}

using FRotator = UE::Math::TRotator<double>;
using FRotator3f = UE::Math::TRotator<float>;
using FQuat = UE::Math::TQuat<double>;
using FQuat4f = UE::Math::TQuat<float>;

template<typename T>
T FMath::ClampAngle(T AngleDegrees, T MinAngleDegrees, T MaxAngleDegrees)
{
	// Mirrors FMath::ClampAngle: clamp around the range's midpoint.
	using R = UE::Math::TRotator<T>;
	const T MaxDelta = R::ClampAxis(MaxAngleDegrees - MinAngleDegrees) * T(0.5);
	const T RangeCenter = R::ClampAxis(MinAngleDegrees + MaxDelta);
	const T DeltaFromCenter = R::NormalizeAxis(AngleDegrees - RangeCenter);
	if (DeltaFromCenter > MaxDelta) { return R::NormalizeAxis(RangeCenter + MaxDelta); }
	if (DeltaFromCenter < -MaxDelta) { return R::NormalizeAxis(RangeCenter - MaxDelta); }
	return R::NormalizeAxis(AngleDegrees);
}

// --- Random -----------------------------------------------------------------------------------

/** Same LCG and float construction as UE's FRandomStream. */
class FRandomStream
{
public:
	FRandomStream() = default;
	explicit FRandomStream(int32 InSeed) { Initialize(InSeed); }

	void Initialize(int32 InSeed) { InitialSeed = InSeed; Seed = uint32(InSeed); }
	void GenerateNewSeed() { static uint32 Counter = 0x9E3779B9u; Initialize(int32(Counter += 0x6D2B79F5u)); }
	int32 GetInitialSeed() const { return InitialSeed; }

	float GetFraction() const
	{
		MutateSeed();
		uint32 Bits = 0x3F800000u | (Seed >> 9);
		float Result;
		std::memcpy(&Result, &Bits, sizeof(Result));
		return Result - 1.f;
	}
	float FRand() const { return GetFraction(); }
	float FRandRange(float Min, float Max) const { return Min + (Max - Min) * FRand(); }
	int32 RandRange(int32 Min, int32 Max) const { const int32 Range = (Max - Min) + 1; return Min + std::min(int32(FRand() * Range), Range - 1); }

	/** Uniform unit vector inside the cone around Dir (same construction as UE: phi = fmod(acos(2v-1), half angle)). */
	FVector VRandCone(const FVector& Dir, float ConeHalfAngleRad) const
	{
		if (ConeHalfAngleRad <= 0.f) { return Dir.GetSafeNormal(); }
		const float RandU = FRand();
		const float RandV = FRand();
		const float Theta = 2.f * UE_PI * RandU;
		float Phi = FMath::Acos((2.f * RandV) - 1.f);
		Phi = FMath::Fmod(Phi, ConeHalfAngleRad);

		const FVector Forward = Dir.GetSafeNormal();
		const FVector Helper = std::abs(Forward.Z) < 0.99 ? FVector::UpVector : FVector::ForwardVector;
		const FVector Right = (Helper ^ Forward).GetSafeNormal();
		const FVector Up = Forward ^ Right;
		const FVector Offset = Right * double(std::cos(Theta)) + Up * double(std::sin(Theta));
		return (Forward * double(std::cos(Phi)) + Offset * double(std::sin(Phi))).GetSafeNormal();
	}

private:
	void MutateSeed() const { Seed = (Seed * 196314165u) + 907633515u; }

	int32 InitialSeed = 0;
	mutable uint32 Seed = 0;
};

// --- UObject surface used by the rules headers -------------------------------------------------

class UObject
{
public:
	virtual ~UObject() = default;
	FName GetFName() const { return Name; }
	void SetFNameForTest(const char* In) { Name = FName(In); }
private:
	FName Name = "Object";
};

template<typename T> using TObjectPtr = T*;

template<typename T>
class TSubclassOf
{
public:
	TSubclassOf() = default;
	explicit operator bool() const { return bSet; }
	void SetForTest(bool bIn) { bSet = bIn; }
private:
	bool bSet = false;
};

template<typename To, typename From>
To* Cast(From* Object) { return dynamic_cast<To*>(Object); }
template<typename To, typename From>
const To* Cast(const From* Object) { return dynamic_cast<const To*>(Object); }

template<typename T>
const T* GetDefault() { static T Default; return &Default; }
