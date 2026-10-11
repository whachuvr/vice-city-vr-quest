#include "common.h"

#include "VrBodyIK.h"
#include "VrRagdoll.h"
#include "Bones.h"
#include "OculusVR.h"
#include "Ped.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "Vehicle.h"
#include "RwHelper.h"
#include "android.h"
#include "main.h"
#include "Timer.h"
#include "Camera.h"
#include "CutsceneMgr.h"

extern bool gVrFirstPersonActive;

#include <android/log.h>
#include <math.h>

// First stage of the body solver: controller-driven arms on the real player
// skeleton. The weapon system remains authoritative and completely untouched.
// We only READ its final visual hand poses so Tommy's wrists follow the same
// tracked hands that are already being rendered.
namespace VrBodyIK
{

static bool gVehicleTransitionActive;

bool
Enabled(void)
{
	return androidgame::VrBodyIKEnabled();
}

bool
VehicleEnabled(void)
{
	return androidgame::VrVehicleBodyIKEnabled();
}

void
SetVehicleTransition(bool active)
{
	gVehicleTransitionActive = active;
}

static bool
BodyIKGameplayContext(void)
{
	// Stereo cutscenes deliberately run through the first-person rendering path,
	// so gVrFirstPersonActive by itself is NOT proof that gameplay IK should run.
	// Native cutscene/scripted animation must own Tommy while those sequences
	// play.
	return !CCutsceneMgr::IsRunning() &&
		!CCutsceneMgr::IsCutsceneProcessing() &&
		!TheCamera.m_WideScreenOn &&
		androidgame::VrCutsceneCameraActor() == nil;
}

bool
WeaponOwnershipActive(void)
{
	// Native vehicle interaction animations own the arms. Do not expose stale
	// solved wrist/weapon links while Tommy opens a door or carjacks a driver.
	if(gVehicleTransitionActive || !BodyIKGameplayContext())
		return false;

	if(Enabled() && OculusVR::IsQuestFirstPersonOnFoot())
		return true;

	CPlayerPed *player = FindPlayerPed();
	return VehicleEnabled() && ::gVrFirstPersonActive &&
		player != nil && player->InVehicle() &&
		player->m_pMyVehicle != nil;
}
namespace
{
#define IKCALLOG(...) __android_log_print(ANDROID_LOG_INFO, "MiamiVR", __VA_ARGS__)
struct WristCalibration
{
	bool valid;
	bool frozen;
	bool clickDown;
	bool lastPoseValid;
	CVector positionLocal;
	CVector rightLocal;
	CVector upLocal;
	CVector atLocal;
	CMatrix frozenPose;
	CMatrix lastPose;
};

struct ArmBones
{
	int clavicle;
	int upper;
	int forearm;
	int hand;
};

struct ShoulderAnchor
{
	bool valid;
	CVector claviclePosLocal;
	CVector upperPosLocal;
	CVector clavicleRightLocal;
	CVector clavicleUpLocal;
	CVector clavicleAtLocal;
};

struct UpperBodyBones
{
	int spine;
	int spine1;
	int neck;
	int head;
};

struct UpperBodyAnchor
{
	bool valid;
	CVector spinePosLocal, spine1PosLocal, neckPosLocal;

	// Native/root-space baseline used only for authored vertical pose changes
	// such as crouching. Walking/idle bob remains suppressed by the stable IK
	// anchors.
	CVector nativeNeckPosLocal;
	CVector spineRightLocal, spineUpLocal, spineAtLocal;
	CVector spine1RightLocal, spine1UpLocal, spine1AtLocal;
	CVector neckRightLocal, neckUpLocal, neckAtLocal;

	// The hidden head bone still influences upper-neck skin weights. Preserve
	// one fixed neck->head relationship so walking animation cannot bend the
	// visible neck toward the camera.
	CVector headFromNeckLocal;
	CVector headRightLocal, headUpLocal, headAtLocal;
};

struct ArmSolveAnchor
{
	bool valid;
	float upperLen;
	float lowerLen;
	CVector upperDirLocal;
	CVector lowerDirLocal;
	CVector elbowOffsetLocal;
	CVector upperRightLocal, upperUpLocal, upperAtLocal;
	CVector foreRightLocal, foreUpLocal, foreAtLocal;
};

struct RigidWeaponHandLink
{
	bool valid;
	CVector positionLocal;
	CVector rightLocal;
	CVector upLocal;
	CVector forwardLocal;
};


static WristCalibration gWrist[2];
static ShoulderAnchor gShoulder[2];
static UpperBodyAnchor gUpperBody;
static ArmSolveAnchor gArmSolve[2];
static RigidWeaponHandLink gRigidWeaponLink[2];
static CMatrix gSolvedHandWorld[2];
static bool gSolvedHandValid[2];

static bool gVehicleNeckValid;
static CVehicle *gVehicleNeckVehicle;
static CVector gVehicleNeckWorld;

static CVehicle *gBikeBodyVehicle;
static bool gBikeBodyRootValid;
static CVector gBikeBodyRootLocal;

// The mount animation can place the native rider root temporarily off-center.
// Do not freeze that first sample, and do not follow the noisy lateral root
// every frame while riding. Wait for a stable seated lateral sample, then latch
// it for the rest of the ride.
static bool gBikeBodyLateralValid;
static float gBikeBodyLateral;
static float gBikeBodyLateralLast;
static float gBikeBodyLateralAccum;
static int gBikeBodyLateralStableFrames;

// Render-time carrier synchronization for wrist POSITION only.
//
// The native player/vehicle carrier can advance between the tracking sample's
// world conversion and CPed::PreRender. Shoulders therefore arrive in the new
// carrier frame while the wrist position can still represent the preceding
// one. Preserve the controller's authoritative orientation, but advance its
// POSITION through exactly the same previous->current body-carrier transform.
static CPed *gHandCarrierPed;
static bool gHandCarrierValid;
static bool gHandCarrierWasVehicle;
static CMatrix gHandCarrierPrev;


enum { MAX_BODY_IK_NODES = 96 };

struct HandDescendantPose
{
	bool valid;
	bool nodeValid[MAX_BODY_IK_NODES];
	CVector posLocal[MAX_BODY_IK_NODES];
	CVector rightLocal[MAX_BODY_IK_NODES];
	CVector upLocal[MAX_BODY_IK_NODES];
	CVector atLocal[MAX_BODY_IK_NODES];
};

static HandDescendantPose gHandPose[2];


// Final baked finger-pose endpoints.
// Runtime L3/R3 grip-pose calibration has been removed permanently.
static const float gHandPoseOpenDeg[2] = { -40.0f, -40.0f };
static const float gHandPoseHoldDeg[2] = { -25.0f, -20.0f };

static float
ResolveHandPoseDeg(int handIndex, float grip)
{
	return gHandPoseOpenDeg[handIndex] +
		(gHandPoseHoldDeg[handIndex]-gHandPoseOpenDeg[handIndex])*grip;
}


struct RoomscaleAnchor
{
	bool valid;
	CVector eyeFromRootLocal;
};

static RoomscaleAnchor gRoomscale;
static CPed *gLastPlayer;

static float ClampF(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static CVector Unit(const CVector &v, const CVector &fallback)
{
	const float len2 = v.MagnitudeSqr();
	if(len2 <= 1.0e-8f)
		return fallback;
	return v * (1.0f / sqrtf(len2));
}

static CVector RotateAroundAxis(const CVector &v, const CVector &axis, float angle)
{
	const float c = cosf(angle);
	const float s = sinf(angle);
	return v*c + CrossProduct(axis, v)*s + axis*(DotProduct(axis, v)*(1.0f-c));
}

static CVector SwingVector(const CVector &fromIn, const CVector &toIn,
	const CVector &value, const CVector &fallbackAxis)
{
	const CVector from = Unit(fromIn, CVector(0.0f, 1.0f, 0.0f));
	const CVector to = Unit(toIn, from);
	const float dot = ClampF(DotProduct(from, to), -1.0f, 1.0f);
	if(dot > 0.99999f)
		return value;
	CVector axis = CrossProduct(from, to);
	if(axis.MagnitudeSqr() <= 1.0e-8f){
		axis = CrossProduct(from, fallbackAxis);
		if(axis.MagnitudeSqr() <= 1.0e-8f)
			axis = CrossProduct(from, CVector(0.0f, 0.0f, 1.0f));
	}
	axis = Unit(axis, CVector(1.0f, 0.0f, 0.0f));
	return RotateAroundAxis(value, axis, acosf(dot));
}

static void Orthonormalise(RwMatrix &m)
{
	CVector at = Unit(m.at, CVector(0.0f, 1.0f, 0.0f));
	CVector right = m.right - at*DotProduct(m.right, at);
	right = Unit(right, CVector(1.0f, 0.0f, 0.0f));
	CVector up = CrossProduct(at, right);
	up = Unit(up, CVector(0.0f, 0.0f, 1.0f));
	right = Unit(CrossProduct(up, at), right);
	m.right = right;
	m.up = up;
	m.at = at;
	m.flags = 0;
}

static CVector ToBasisLocal(const CVector &world, const CMatrix &basis)
{
	return CVector(DotProduct(world, basis.GetRight()),
		DotProduct(world, basis.GetUp()),
		DotProduct(world, basis.GetForward()));
}

static CVector FromBasisLocal(const CVector &local, const CMatrix &basis)
{
	return basis.GetRight()*local.x + basis.GetUp()*local.y +
		basis.GetForward()*local.z;
}

static CVector
AdvancePositionBetweenCarriers(const CVector &worldPosition,
	const CMatrix &fromCarrier, const CMatrix &toCarrier)
{
	const CVector local =
		ToBasisLocal(worldPosition-fromCarrier.GetPosition(), fromCarrier);
	return toCarrier.GetPosition()+
		FromBasisLocal(local, toCarrier);
}

static bool
CarrierDeltaIsReasonable(const CMatrix &fromCarrier,
	const CMatrix &toCarrier)
{
	const CVector delta =
		toCarrier.GetPosition()-fromCarrier.GetPosition();

	// A normal rendered locomotion/vehicle step is tiny. A large discontinuity
	// is a teleport, scene transition, respawn, mount reset, etc. Never feed
	// that through an arm solve.
	if(delta.MagnitudeSqr() > 4.0f) // > 2 metres in one render
		return false;

	const CVector a = fromCarrier.GetForward();
	const CVector b = toCarrier.GetForward();
	if(a.MagnitudeSqr() <= 1.0e-8f || b.MagnitudeSqr() <= 1.0e-8f)
		return false;

	CVector an = a, bn = b;
	an.Normalise();
	bn.Normalise();

	// Also reject an essentially instantaneous 120+ degree carrier flip.
	return DotProduct(an, bn) > -0.5f;
}

static bool BuildBikeCarrierBasis(CVehicle *bike, const CMatrix &nativeBody,
	CMatrix &out)
{
	if(bike == nil || !bike->IsBike())
		return false;

	out = nativeBody;
	CVector forward = bike->GetForward();
	CVector up = bike->GetUp();
	if(forward.MagnitudeSqr() <= 1.0e-8f ||
	   up.MagnitudeSqr() <= 1.0e-8f)
		return false;

	forward.Normalise();
	up.Normalise();
	CVector right = CrossProduct(forward, up);
	if(right.MagnitudeSqr() <= 1.0e-8f)
		return false;
	right.Normalise();
	up = CrossProduct(right, forward);
	if(up.MagnitudeSqr() <= 1.0e-8f)
		return false;
	up.Normalise();

	out.GetRight() = right;
	out.GetUp() = up;
	out.GetForward() = forward;
	return true;
}

static void CaptureRigidWeaponLink(int hand, const CMatrix &solvedHand,
	const CMatrix &weaponHandBasis)
{
	if(hand < 0 || hand > 1 || gRigidWeaponLink[hand].valid)
		return;

	RigidWeaponHandLink &link = gRigidWeaponLink[hand];
	link.positionLocal = ToBasisLocal(
		weaponHandBasis.GetPosition()-solvedHand.GetPosition(), solvedHand);
	link.rightLocal = ToBasisLocal(weaponHandBasis.GetRight(), solvedHand);
	link.upLocal = ToBasisLocal(weaponHandBasis.GetUp(), solvedHand);
	link.forwardLocal = ToBasisLocal(weaponHandBasis.GetForward(), solvedHand);
	link.valid = true;
}

static bool RebuildRigidWeaponBasis(int hand, CVector *position,
	CVector *right, CVector *up, CVector *forward)
{
	if(hand < 0 || hand > 1 || !position || !right || !up || !forward ||
	   !WeaponOwnershipActive() ||
	   !gSolvedHandValid[hand] || !gRigidWeaponLink[hand].valid)
		return false;

	const CMatrix &wrist = gSolvedHandWorld[hand];
	const RigidWeaponHandLink &link = gRigidWeaponLink[hand];

	// Reconstruct the original raw weapon-hand presentation axes from the
	// final solved wrist. THESE are the physical weapon/palm axes; the
	// skinned wrist's own Right axis is a model-bone axis and is not a
	// reliable left/right translation direction.
	*right = FromBasisLocal(link.rightLocal, wrist);
	*up = FromBasisLocal(link.upLocal, wrist);
	*forward = FromBasisLocal(link.forwardLocal, wrist);

	if(right->MagnitudeSqr() <= 1.0e-8f ||
	   up->MagnitudeSqr() <= 1.0e-8f ||
	   forward->MagnitudeSqr() <= 1.0e-8f)
		return false;

	right->Normalise();
	up->Normalise();
	forward->Normalise();

	*position = wrist.GetPosition() +
		FromBasisLocal(link.positionLocal, wrist);

	// LEFT IK weapon alignment:
	// The v33 aim/laser parity is already correct relative to the final gun.
	// The remaining error is that the entire LEFT rigid gun+laser presentation
	// points slightly left. Correct the FINAL rigid hand basis here, so weapon
	// model and v33-localized laser move together and their relationship is
	// untouched.
	if(hand == 0){
		const float leftWeaponYawCorrection =
			DEGTORAD(2.5f); // rightward around the physical weapon Up axis
		*right = RotateAroundAxis(
			*right, *up, leftWeaponYawCorrection);
		*forward = RotateAroundAxis(
			*forward, *up, leftWeaponYawCorrection);

		if(right->MagnitudeSqr() <= 1.0e-8f ||
		   forward->MagnitudeSqr() <= 1.0e-8f)
			return false;
		right->Normalise();
		forward->Normalise();
	}

	return true;
}


static void OrthonormaliseBasis(CMatrix &m)
{
	CVector at = Unit(m.GetForward(), CVector(0.0f, 1.0f, 0.0f));
	CVector right = m.GetRight() - at*DotProduct(m.GetRight(), at);
	right = Unit(right, CVector(1.0f, 0.0f, 0.0f));
	CVector up = Unit(CrossProduct(at, right), CVector(0.0f, 0.0f, 1.0f));
	right = Unit(CrossProduct(up, at), right);
	m.GetRight() = right;
	m.GetUp() = up;
	m.GetForward() = at;
}

static void MatrixToQuaternion(const CMatrix &m, float &qx, float &qy,
	float &qz, float &qw)
{
	const float m00 = m.GetRight().x,   m01 = m.GetUp().x,      m02 = m.GetForward().x;
	const float m10 = m.GetRight().y,   m11 = m.GetUp().y,      m12 = m.GetForward().y;
	const float m20 = m.GetRight().z,   m21 = m.GetUp().z,      m22 = m.GetForward().z;
	const float trace = m00 + m11 + m22;
	if(trace > 0.0f){
		const float q = sqrtf(trace + 1.0f)*2.0f;
		qw = 0.25f*q; qx = (m21-m12)/q; qy = (m02-m20)/q; qz = (m10-m01)/q;
	}else if(m00 > m11 && m00 > m22){
		const float q = sqrtf(1.0f + m00-m11-m22)*2.0f;
		qw = (m21-m12)/q; qx = 0.25f*q; qy = (m01+m10)/q; qz = (m02+m20)/q;
	}else if(m11 > m22){
		const float q = sqrtf(1.0f + m11-m00-m22)*2.0f;
		qw = (m02-m20)/q; qx = (m01+m10)/q; qy = 0.25f*q; qz = (m12+m21)/q;
	}else{
		const float q = sqrtf(1.0f + m22-m00-m11)*2.0f;
		qw = (m10-m01)/q; qx = (m02+m20)/q; qy = (m12+m21)/q; qz = 0.25f*q;
	}
}

static void LogCalibration(int handIndex, const WristCalibration &cal)
{
	CMatrix local;
	local.GetPosition() = cal.positionLocal;
	local.GetRight() = cal.rightLocal;
	local.GetUp() = cal.upLocal;
	local.GetForward() = cal.atLocal;
	OrthonormaliseBasis(local);

	float qx, qy, qz, qw;
	MatrixToQuaternion(local, qx, qy, qz, qw);

	const float pitch = asinf(ClampF(-local.GetRight().z, -1.0f, 1.0f));
	const float yaw = atan2f(local.GetRight().y, local.GetRight().x);
	const float roll = atan2f(local.GetUp().z, local.GetForward().z);
	const float radToDeg = 57.29577951308232f;
	const char *name = handIndex == 0 ? "LEFT" : "RIGHT";

	IKCALLOG("[IKCAL] %s RESULT pos=(%.6f,%.6f,%.6f) "
		"right=(%.6f,%.6f,%.6f) up=(%.6f,%.6f,%.6f) "
		"forward=(%.6f,%.6f,%.6f) quat=(%.6f,%.6f,%.6f,%.6f) "
		"eulerDeg=(pitch=%.3f,yaw=%.3f,roll=%.3f)",
		name,
		cal.positionLocal.x, cal.positionLocal.y, cal.positionLocal.z,
		cal.rightLocal.x, cal.rightLocal.y, cal.rightLocal.z,
		cal.upLocal.x, cal.upLocal.y, cal.upLocal.z,
		cal.atLocal.x, cal.atLocal.y, cal.atLocal.z,
		qx, qy, qz, qw,
		pitch*radToDeg, yaw*radToDeg, roll*radToDeg);
}

static CMatrix ApplyCalibration(const CMatrix &raw, const WristCalibration &cal)
{
	CMatrix out;
	out.GetPosition() = raw.GetPosition() + FromBasisLocal(cal.positionLocal, raw);
	out.GetRight() = FromBasisLocal(cal.rightLocal, raw);
	out.GetUp() = FromBasisLocal(cal.upLocal, raw);
	out.GetForward() = FromBasisLocal(cal.atLocal, raw);
	OrthonormaliseBasis(out);
	return out;
}

static void CaptureCalibration(int handIndex, const CMatrix &raw, WristCalibration &cal)
{
	cal.positionLocal = ToBasisLocal(cal.frozenPose.GetPosition()-raw.GetPosition(), raw);
	cal.rightLocal = ToBasisLocal(cal.frozenPose.GetRight(), raw);
	cal.upLocal = ToBasisLocal(cal.frozenPose.GetUp(), raw);
	cal.atLocal = ToBasisLocal(cal.frozenPose.GetForward(), raw);
	cal.valid = true;
	cal.frozen = false;
	gRigidWeaponLink[handIndex] = RigidWeaponHandLink();
	gSolvedHandValid[handIndex] = false;
	LogCalibration(handIndex, cal);
}

static bool BuildGroundedVisualBody(CPed *ped,
	RpHAnimHierarchy *hier, RwMatrix *mats, CMatrix &visualBody)
{
	visualBody = ped->GetMatrix();
	const CVector nativeRoot = ped->GetPosition();
	visualBody.GetPosition() = nativeRoot;

	float eyePosRaw[3];
	float rightRaw[3], upRaw[3], forwardRaw[3];
	if(androidgame::VrGetViewBasis(
	       eyePosRaw, rightRaw, upRaw, forwardRaw)){
		CVector up = visualBody.GetUp();
		up = Unit(up, CVector(0.0f, 0.0f, 1.0f));

		CVector forward(forwardRaw[0], forwardRaw[1], forwardRaw[2]);
		forward -= up*DotProduct(forward, up);
		if(forward.MagnitudeSqr() > 1.0e-8f){
			forward.Normalise();

			CVector right = CrossProduct(forward, up);
			if(right.MagnitudeSqr() > 1.0e-8f){
				right.Normalise();

				const float yawCorrection = DEGTORAD(-6.0f);
				const float cy = cosf(yawCorrection);
				const float sy = sinf(yawCorrection);

				CVector correctedForward =
					forward*cy + right*sy;
				correctedForward -=
					up*DotProduct(correctedForward, up);
				correctedForward =
					Unit(correctedForward, forward);

				CVector correctedRight =
					CrossProduct(correctedForward, up);
				correctedRight =
					Unit(correctedRight, right);
				up = CrossProduct(
					correctedRight, correctedForward);
				up.Normalise();

				visualBody.GetRight() = correctedRight;
				visualBody.GetUp() = up;
				visualBody.GetForward() = correctedForward;
			}
		}

		float anchorRaw[3];
		if(androidgame::VrGetOnFootBodyAnchor(anchorRaw)){
			const CVector eye(
				eyePosRaw[0], eyePosRaw[1], eyePosRaw[2]);
			const CVector anchor(
				anchorRaw[0], anchorRaw[1], anchorRaw[2]);

			CVector delta = eye-anchor;
			delta.z = 0.0f;

			const float kRoomscaleRadius = 0.12f;
			const float horizontal =
				sqrtf(delta.x*delta.x + delta.y*delta.y);

			if(horizontal > kRoomscaleRadius &&
			   horizontal > 0.000001f){
				const float excess =
					horizontal-kRoomscaleRadius;
				const float inv = 1.0f/horizontal;
				const CVector follow(
					delta.x*inv*excess,
					delta.y*inv*excess,
					0.0f);

				visualBody.GetPosition() = nativeRoot+follow;

				// Apply the exact same horizontal body-follow offset to every
				// rendered bone. No Z translation, no controller transform,
				// and no camera transform.
				for(int i = 0; i < hier->numNodes; i++){
					mats[i].pos.x += follow.x;
					mats[i].pos.y += follow.y;
				}
			}
		}
	}

	return true;
}

static bool ResolveUpperBody(RpHAnimHierarchy *hier, UpperBodyBones &out)
{
	out.spine = RpHAnimIDGetIndex(hier, BONE_spine);
	out.spine1 = RpHAnimIDGetIndex(hier, BONE_spine1);
	out.neck = RpHAnimIDGetIndex(hier, BONE_neck);
	out.head = RpHAnimIDGetIndex(hier, BONE_head);
	return out.spine >= 0 && out.spine1 >= 0 && out.neck >= 0 && out.head >= 0 &&
		out.spine < hier->numNodes && out.spine1 < hier->numNodes &&
		out.neck < hier->numNodes && out.head < hier->numNodes;
}

static bool StabilizeUpperBody(CPed *ped, RwMatrix *mats,
	const UpperBodyBones &b, const CMatrix &visualBody,
	float *authoredVerticalOffset)
{
	const CMatrix &nativeBody = ped->GetMatrix();
	const CVector nativeRoot = ped->GetPosition();
	const CVector visualRoot = visualBody.GetPosition();
	RwMatrix &spine = mats[b.spine];
	RwMatrix &spine1 = mats[b.spine1];
	RwMatrix &neck = mats[b.neck];
	RwMatrix &head = mats[b.head];

	if(!gUpperBody.valid){
		gUpperBody.spinePosLocal =
			ToBasisLocal(CVector(spine.pos)-visualRoot, visualBody);
		gUpperBody.spine1PosLocal =
			ToBasisLocal(CVector(spine1.pos)-visualRoot, visualBody);
		gUpperBody.neckPosLocal =
			ToBasisLocal(CVector(neck.pos)-visualRoot, visualBody);
		gUpperBody.nativeNeckPosLocal =
			ToBasisLocal(CVector(neck.pos)-nativeRoot, nativeBody);

		gUpperBody.spineRightLocal = ToBasisLocal(spine.right, visualBody);
		gUpperBody.spineUpLocal = ToBasisLocal(spine.up, visualBody);
		gUpperBody.spineAtLocal = ToBasisLocal(spine.at, visualBody);

		gUpperBody.spine1RightLocal = ToBasisLocal(spine1.right, visualBody);
		gUpperBody.spine1UpLocal = ToBasisLocal(spine1.up, visualBody);
		gUpperBody.spine1AtLocal = ToBasisLocal(spine1.at, visualBody);

		gUpperBody.neckRightLocal = ToBasisLocal(neck.right, visualBody);
		gUpperBody.neckUpLocal = ToBasisLocal(neck.up, visualBody);
		gUpperBody.neckAtLocal = ToBasisLocal(neck.at, visualBody);

		// Capture the head relative to the native neck once. Even though the
		// head geometry is hidden in first person, its skin weights can pull
		// the top of the visible neck if this bone keeps animating.
		gUpperBody.headFromNeckLocal =
			ToBasisLocal(CVector(head.pos)-CVector(neck.pos), nativeBody);
		gUpperBody.headRightLocal = ToBasisLocal(head.right, nativeBody);
		gUpperBody.headUpLocal = ToBasisLocal(head.up, nativeBody);
		gUpperBody.headAtLocal = ToBasisLocal(head.at, nativeBody);

		gUpperBody.valid = true;
	}

	// Preserve deliberate authored vertical lowering (crouch) without restoring
	// the walking/idle bob that the stable body was designed to remove.
	//
	// ToBasisLocal uses x=Right, y=Up, z=Forward. Native walking bob is normally
	// only a couple of centimetres, so a 2 cm soft dead-zone filters it. A real
	// crouch continues well beyond that and passes through continuously.
	const CVector currentNativeNeckLocal =
		ToBasisLocal(CVector(neck.pos)-nativeRoot, nativeBody);
	const float nativeVerticalDelta =
		currentNativeNeckLocal.y-gUpperBody.nativeNeckPosLocal.y;
	const float crouchOffset =
		nativeVerticalDelta < -0.020f ?
			ClampF(nativeVerticalDelta+0.020f, -0.55f, 0.0f) : 0.0f;
	if(authoredVerticalOffset)
		*authoredVerticalOffset = crouchOffset;

	CVector spineLocal = gUpperBody.spinePosLocal;
	CVector spine1Local = gUpperBody.spine1PosLocal;
	spineLocal.y += crouchOffset;
	spine1Local.y += crouchOffset;

	spine.pos = visualRoot + FromBasisLocal(spineLocal, visualBody);
	spine.right = FromBasisLocal(gUpperBody.spineRightLocal, visualBody);
	spine.up = FromBasisLocal(gUpperBody.spineUpLocal, visualBody);
	spine.at = FromBasisLocal(gUpperBody.spineAtLocal, visualBody);
	Orthonormalise(spine);

	spine1.pos = visualRoot + FromBasisLocal(spine1Local, visualBody);
	spine1.right = FromBasisLocal(gUpperBody.spine1RightLocal, visualBody);
	spine1.up = FromBasisLocal(gUpperBody.spine1UpLocal, visualBody);
	spine1.at = FromBasisLocal(gUpperBody.spine1AtLocal, visualBody);
	Orthonormalise(spine1);

	// IMPORTANT: camera anchoring in android.cpp is reconstructed from Tommy's
	// native/root basis. Keep the neck POSITION in that exact same frame so HMD
	// yaw stabilization cannot swing the neck forward across the camera while
	// walking. Rotation may still follow the stabilized upper-body basis.
	const CMatrix &nativeBodyForNeck = ped->GetMatrix();
	const CVector nativeRootForNeck = ped->GetPosition();
	CVector neckLocal = gUpperBody.neckPosLocal;
	neckLocal.y += crouchOffset;
	neck.pos = nativeRootForNeck +
		FromBasisLocal(neckLocal, nativeBodyForNeck);

	// Keep the visible neckline filled but tuck only the exposed stub down.
	CVector neckRetractUp = nativeBodyForNeck.GetUp();
	neckRetractUp = Unit(neckRetractUp, CVector(0.0f, 0.0f, 1.0f));
	neck.pos = CVector(neck.pos) - neckRetractUp*0.025f;

	neck.right = FromBasisLocal(gUpperBody.neckRightLocal, visualBody);
	neck.up = FromBasisLocal(gUpperBody.neckUpLocal, visualBody);
	neck.at = FromBasisLocal(gUpperBody.neckAtLocal, visualBody);
	Orthonormalise(neck);

	// Lock the hidden head to the already-locked neck. This removes the last
	// walking-animation transform that can drag upper-neck weighted vertices
	// forward into the HMD.
	head.pos = CVector(neck.pos) +
		FromBasisLocal(gUpperBody.headFromNeckLocal, nativeBodyForNeck);
	head.right = FromBasisLocal(gUpperBody.headRightLocal, visualBody);
	head.up = FromBasisLocal(gUpperBody.headUpLocal, visualBody);
	head.at = FromBasisLocal(gUpperBody.headAtLocal, visualBody);
	Orthonormalise(head);

	return true;
}

static bool ResolveArm(RpHAnimHierarchy *hier, bool left, ArmBones &out)
{
	const int clav = left ? BONE_l_clavicle : BONE_r_clavicle;
	const int upper = left ? BONE_l_upperarm : BONE_r_upperarm;
	const int forearm = left ? BONE_l_forearm : BONE_r_forearm;
	const int hand = left ? BONE_l_hand : BONE_r_hand;
	out.clavicle = RpHAnimIDGetIndex(hier, clav);
	out.upper = RpHAnimIDGetIndex(hier, upper);
	out.forearm = RpHAnimIDGetIndex(hier, forearm);
	out.hand = RpHAnimIDGetIndex(hier, hand);
	return out.clavicle >= 0 && out.upper >= 0 && out.forearm >= 0 &&
		out.hand >= 0 && out.clavicle < hier->numNodes &&
		out.upper < hier->numNodes && out.forearm < hier->numNodes &&
		out.hand < hier->numNodes;
}

static void StabilizeShoulderRoot(CPed *ped, RwMatrix *mats,
	const ArmBones &b, int handIndex, const CMatrix &visualBody,
	float authoredVerticalOffset)
{
	ShoulderAnchor &anchor = gShoulder[handIndex];
	const CMatrix &nativeBody = ped->GetMatrix();
	const CVector nativeRoot = ped->GetPosition();
	RwMatrix &clavicle = mats[b.clavicle];
	RwMatrix &upper = mats[b.upper];

	if(!anchor.valid){
		anchor.claviclePosLocal =
			ToBasisLocal(CVector(clavicle.pos)-nativeRoot, nativeBody);
		anchor.upperPosLocal =
			ToBasisLocal(CVector(upper.pos)-nativeRoot, nativeBody);
		anchor.clavicleRightLocal =
			ToBasisLocal(clavicle.right, nativeBody);
		anchor.clavicleUpLocal =
			ToBasisLocal(clavicle.up, nativeBody);
		anchor.clavicleAtLocal =
			ToBasisLocal(clavicle.at, nativeBody);
		anchor.valid = true;
	}

	const CVector root = visualBody.GetPosition();
	CVector clavicleLocal = anchor.claviclePosLocal;
	CVector upperLocal = anchor.upperPosLocal;
	clavicleLocal.y += authoredVerticalOffset;
	upperLocal.y += authoredVerticalOffset;

	clavicle.pos = root +
		FromBasisLocal(clavicleLocal, visualBody);
	clavicle.right =
		FromBasisLocal(anchor.clavicleRightLocal, visualBody);
	clavicle.up =
		FromBasisLocal(anchor.clavicleUpLocal, visualBody);
	clavicle.at =
		FromBasisLocal(anchor.clavicleAtLocal, visualBody);
	Orthonormalise(clavicle);

	upper.pos = root +
		FromBasisLocal(upperLocal, visualBody);
}

static void BuildParentTable(RpHAnimHierarchy *hier, int *parentOf)
{
	// Mirror RenderWare's hierarchy traversal. The skin matrices are already
	// world-space here, so unsolved child bones must be carried explicitly
	// whenever their solved parent moves.
	int stack[96];
	int sp = 0;
	int parent = -1;
	stack[sp++] = parent;
	for(int i = 0; i < hier->numNodes; i++){
		parentOf[i] = parent;
		const int flags = hier->nodeInfo[i].flags;
		if((flags & rw::HAnimHierarchy::PUSH) && sp < 96)
			stack[sp++] = parent;
		parent = i;
		if((flags & rw::HAnimHierarchy::POP) && sp > 0)
			parent = stack[--sp];
	}
}

static bool IsDescendantOf(int node, int ancestor, const int *parentOf)
{
	for(int p = parentOf[node]; p >= 0; p = parentOf[p])
		if(p == ancestor)
			return true;
	return false;
}

static void RotateHandBasis(RwMatrix &m, const CVector &axis, float radians);

static CMatrix
MatrixFromRw(const RwMatrix &m)
{
	CMatrix out;
	out.GetPosition() = CVector(m.pos);
	out.GetRight() = CVector(m.right);
	out.GetUp() = CVector(m.up);
	out.GetForward() = CVector(m.at);
	OrthonormaliseBasis(out);
	return out;
}

static bool
CaptureStaticHandPose(RpHAnimHierarchy *hier, RwMatrix *mats,
	int handNode, int handIndex, const int *parentOf)
{
	if(handIndex < 0 || handIndex > 1 || hier->numNodes <= 0 ||
	   hier->numNodes > MAX_BODY_IK_NODES)
		return false;

	HandDescendantPose &pose = gHandPose[handIndex];
	if(pose.valid)
		return true;

	for(int i = 0; i < MAX_BODY_IK_NODES; i++)
		pose.nodeValid[i] = false;

	for(int i = 0; i < hier->numNodes; i++){
		if(i == handNode || !IsDescendantOf(i, handNode, parentOf))
			continue;

		const int parent = parentOf[i];
		if(parent < 0 || parent >= hier->numNodes)
			continue;

		const CMatrix parentBasis = MatrixFromRw(mats[parent]);
		pose.posLocal[i] = ToBasisLocal(
			CVector(mats[i].pos)-CVector(mats[parent].pos), parentBasis);
		pose.rightLocal[i] =
			ToBasisLocal(CVector(mats[i].right), parentBasis);
		pose.upLocal[i] =
			ToBasisLocal(CVector(mats[i].up), parentBasis);
		pose.atLocal[i] =
			ToBasisLocal(CVector(mats[i].at), parentBasis);
		pose.nodeValid[i] = true;
	}

	pose.valid = true;
	return true;
}

static int
HandDescendantDepth(int node, int handNode, const int *parentOf)
{
	int depth = 0;
	for(int p = node; p >= 0 && p != handNode; p = parentOf[p])
		depth++;
	return depth;
}

static void
ApplyStaticGripHandPose(RpHAnimHierarchy *hier, RwMatrix *mats,
	int handNode, int handIndex, const int *parentOf,
	const CVector &physicalCurlAxis, float gripAmount)
{
	if(handIndex < 0 || handIndex > 1 ||
	   !gHandPose[handIndex].valid ||
	   hier->numNodes <= 0 || hier->numNodes > MAX_BODY_IK_NODES)
		return;

	HandDescendantPose &pose = gHandPose[handIndex];

	// Quest grip is analogue. Keep a small deadzone, then smoothly curl into
	// one deterministic holding pose. Native walking/idle animation has no
	// influence after the one-time open-pose capture.
	const float grip = ClampF((gripAmount-0.12f)/0.58f, 0.0f, 1.0f);
	const CVector curlAxis = Unit(
		physicalCurlAxis, CVector(1.0f, 0.0f, 0.0f));

	for(int i = 0; i < hier->numNodes; i++){
		if(!pose.nodeValid[i] ||
		   i == handNode || !IsDescendantOf(i, handNode, parentOf))
			continue;

		const int parent = parentOf[i];
		if(parent < 0 || parent >= hier->numNodes)
			continue;

		const CMatrix parentBasis = MatrixFromRw(mats[parent]);

		mats[i].pos = CVector(mats[parent].pos) +
			FromBasisLocal(pose.posLocal[i], parentBasis);
		mats[i].right =
			FromBasisLocal(pose.rightLocal[i], parentBasis);
		mats[i].up =
			FromBasisLocal(pose.upLocal[i], parentBasis);
		mats[i].at =
			FromBasisLocal(pose.atLocal[i], parentBasis);
		mats[i].flags = 0;
		Orthonormalise(mats[i]);

		// Deterministic OPEN -> CLOSED pose.
		//
		// The captured native snapshot is only a neutral geometry reference;
		// it is NOT treated as the open pose anymore. At grip=0 explicitly
		// extend the finger chain. As grip rises, smoothly move through neutral
		// and into the holding curl. This guarantees visible opening even if
		// Tommy's first captured animation frame had partially curled fingers.
		const int depth = HandDescendantDepth(i, handNode, parentOf);

		// Final baked OPEN and HOLD are the only two endpoints. There is no
		// runtime calibration and no invented extra/max-closed state.
		// Grip interpolates exactly between these fixed values.
		const float basePoseDeg =
			ResolveHandPoseDeg(handIndex, grip);

		// One calibration value controls the finger chain while preserving the
		// existing natural increase in bend toward the fingertip.
		float depthScale = 1.0f;
		if(depth >= 2)
			depthScale = 1.625f;
		if(depth >= 3)
			depthScale = 2.125f;

		const float handMirror =
			(handIndex == 0) ? -1.0f : 1.0f;
		const float poseDeg =
			basePoseDeg*depthScale*handMirror;

		if(fabsf(poseDeg) > 0.001f)
			RotateHandBasis(
				mats[i], curlAxis,
				DEGTORAD(poseDeg));
	}
}


static bool GetFloatingHandPresentationPose(int handIndex, CMatrix &pose)
{
	// IMPORTANT: use the exact refreshed raw grip + aim matrices that
	// BuildTrackedWeaponHandBasis() uses for weapon rendering. Do not mix an
	// interaction-adjusted hand matrix with a separately sourced aim ray.
	CMatrix grip, aim;
	if(!OculusVR::GetQuestRawTrackedHandMatrix(handIndex, &grip) ||
	   !OculusVR::GetQuestRawTrackedHandAimMatrix(handIndex, &aim))
		return false;

	CVector forward = aim.GetForward();
	if(forward.MagnitudeSqr() < 1.0e-8f)
		forward = grip.GetForward();
	if(forward.MagnitudeSqr() < 1.0e-8f)
		return false;
	forward.Normalise();

	const int modelHand = 1-handIndex;
	CVector up =
		grip.GetRight()*(modelHand == 0 ? 1.0f : -1.0f);
	up -= forward*DotProduct(up, forward);
	if(up.MagnitudeSqr() < 1.0e-8f)
		up = grip.GetUp();
	up.Normalise();

	CVector right = CrossProduct(up, forward);
	if(right.MagnitudeSqr() < 1.0e-8f)
		return false;
	right.Normalise();
	if(DotProduct(right, grip.GetForward()) < 0.0f)
		right *= -1.0f;

	pose.GetPosition() = grip.GetPosition();
	pose.GetRight() = right;
	pose.GetUp() = up;
	pose.GetForward() = forward;
	OrthonormaliseBasis(pose);
	return true;
}

static bool GetVehicleHandPresentationPose(int handIndex, CMatrix &pose)
{
	// A free hand in a vehicle must behave EXACTLY like the normal tracked
	// on-foot hand. Do not send it through GetTrackedVisualHandMatrix(), because
	// that path applies wheel presentation/pull-back offsets even after the hand
	// has left the steering control.
	if(!OculusVR::IsImmersiveSteeringHandleGrabbed(handIndex))
		return GetFloatingHandPresentationPose(handIndex, pose);

	// Only an actively grabbed steering hand uses the vehicle socket POSITION.
	// Its base wrist orientation remains the normal calibrated tracked pose,
	// with only the physical steering-angle rotation added.
	CMatrix vehicleTarget;
	float grip = 0.0f, trigger = 0.0f;
	if(!OculusVR::GetTrackedVisualHandMatrix(
	   handIndex, &vehicleTarget, &grip, &trigger))
		return false;

	CMatrix onFootPose;
	if(!GetFloatingHandPresentationPose(handIndex, onFootPose))
		return false;

	pose = onFootPose;

	CVector steerAxis;
	float steerAngle = 0.0f;
	if(OculusVR::GetQuestVehicleHandSteerRotation(
	   handIndex, &steerAxis, &steerAngle) &&
	   steerAxis.MagnitudeSqr() > 1.0e-8f &&
	   fabsf(steerAngle) > 1.0e-6f){
		steerAxis.Normalise();
		pose.GetRight() = RotateAroundAxis(
			pose.GetRight(), steerAxis, steerAngle);
		pose.GetUp() = RotateAroundAxis(
			pose.GetUp(), steerAxis, steerAngle);
		pose.GetForward() = RotateAroundAxis(
			pose.GetForward(), steerAxis, steerAngle);
	}

	pose.GetPosition() = vehicleTarget.GetPosition();
	OrthonormaliseBasis(pose);
	return true;
}


static void RotateHandBasis(RwMatrix &hand, const CVector &axis, float angle)
{
	CVector n = Unit(axis, CVector(1.0f, 0.0f, 0.0f));
	hand.right = RotateAroundAxis(hand.right, n, angle);
	hand.up = RotateAroundAxis(hand.up, n, angle);
	hand.at = RotateAroundAxis(hand.at, n, angle);
	Orthonormalise(hand);
}

static CMatrix BuildDefaultHandPose(int handIndex, const CMatrix &tracked)
{
	CMatrix out;
	out.GetPosition() = tracked.GetPosition();

	CVector finger = Unit(tracked.GetForward(), CVector(0.0f, 1.0f, 0.0f));
	CVector palm = Unit(tracked.GetUp(), CVector(0.0f, 0.0f, 1.0f));
	if(handIndex == 1)
		palm *= -1.0f;
	palm -= finger*DotProduct(palm, finger);
	palm = Unit(palm, CVector(handIndex == 0 ? 1.0f : -1.0f, 0.0f, 0.0f));
	CVector across = Unit(CrossProduct(palm, finger), tracked.GetRight());

	out.GetRight() = across;
	out.GetUp() = palm;
	out.GetForward() = finger;
	OrthonormaliseBasis(out);

	// Preserve the v10 rough left-hand neutral only until a measured calibration
	// is captured. The measured transform then replaces it completely.
	if(handIndex == 0){
		RwMatrix tmp;
		tmp.right = out.GetRight();
		tmp.up = out.GetUp();
		tmp.at = out.GetForward();
		tmp.pos = out.GetPosition();
		RotateHandBasis(tmp, tracked.GetUp(), DEGTORAD(180.0f));
		out.GetRight() = tmp.right;
		out.GetUp() = tmp.up;
		out.GetForward() = tmp.at;
	}
	return out;
}

static bool CaptureArmSolveAnchor(CPed *ped, RwMatrix *mats,
	const ArmBones &b, int handIndex)
{
	ArmSolveAnchor &cal = gArmSolve[handIndex];
	if(cal.valid)
		return true;

	const CMatrix &nativeBody = ped->GetMatrix();
	const CVector shoulder = mats[b.upper].pos;
	const CVector elbow = mats[b.forearm].pos;
	const CVector wrist = mats[b.hand].pos;
	const CVector upperDir = elbow-shoulder;
	const CVector lowerDir = wrist-elbow;

	cal.upperLen = upperDir.Magnitude();
	cal.lowerLen = lowerDir.Magnitude();
	if(cal.upperLen < 0.03f || cal.lowerLen < 0.03f)
		return false;

	cal.upperDirLocal = ToBasisLocal(upperDir, nativeBody);
	cal.lowerDirLocal = ToBasisLocal(lowerDir, nativeBody);
	cal.elbowOffsetLocal = ToBasisLocal(elbow-shoulder, nativeBody);

	cal.upperRightLocal =
		ToBasisLocal(mats[b.upper].right, nativeBody);
	cal.upperUpLocal =
		ToBasisLocal(mats[b.upper].up, nativeBody);
	cal.upperAtLocal =
		ToBasisLocal(mats[b.upper].at, nativeBody);

	cal.foreRightLocal =
		ToBasisLocal(mats[b.forearm].right, nativeBody);
	cal.foreUpLocal =
		ToBasisLocal(mats[b.forearm].up, nativeBody);
	cal.foreAtLocal =
		ToBasisLocal(mats[b.forearm].at, nativeBody);

	cal.valid = true;
	return true;
}

static void
StretchBoneAlongWorldDirection(RwMatrix &bone, const CVector &worldDirection,
	float stretch)
{
	if(stretch <= 1.0001f || worldDirection.MagnitudeSqr() <= 1.0e-8f)
		return;

	CVector axis = worldDirection;
	axis.Normalise();
	const float extra = stretch-1.0f;

	// Directional scale:
	//   v' = v + axis * dot(v,axis) * (stretch-1)
	//
	// Only the component parallel to the bone's longitudinal direction grows.
	// Components perpendicular to the forearm are unchanged, so the mesh gains
	// length without becoming skinny/narrow.
	CVector r = bone.right;
	CVector u = bone.up;
	CVector a = bone.at;

	r += axis * (DotProduct(r,axis)*extra);
	u += axis * (DotProduct(u,axis)*extra);
	a += axis * (DotProduct(a,axis)*extra);

	bone.right = r;
	bone.up = u;
	bone.at = a;
}

static bool SolveArm(RpHAnimHierarchy *hier, RwMatrix *mats,
	const int *parentOf, const ArmBones &b, int handIndex,
	const CMatrix &tracked, const CMatrix &body,
	const CVector &physicalPitchAxis, bool pitchWeaponHand,
	float gripAmount, bool preserveWeaponContact)
{
	RwMatrix &upper = mats[b.upper];
	RwMatrix &forearm = mats[b.forearm];
	RwMatrix &hand = mats[b.hand];
	const RwMatrix oldHand = hand;

	// Capture the descendant pose exactly once from the untouched native
	// skeleton, then never consume walking/idle finger animation again.
	CaptureStaticHandPose(hier, mats, b.hand, handIndex, parentOf);

	ArmSolveAnchor &cal = gArmSolve[handIndex];
	const CVector shoulder = upper.pos;
	if(!cal.valid)
		return false;
	const CVector target = tracked.GetPosition();
	CVector delta = target-shoulder;
	float targetDistance = delta.Magnitude();
	if(targetDistance < 0.02f) return false;
	const CVector aim = delta*(1.0f/targetDistance);
	// Natural reach first uses the measured arm lengths. If the target is a
	// little farther away (common with deeper steering wheels), put that extra
	// reach into the FOREARM segment instead of leaving a gap between the end
	// of the forearm and the tracked hand.
	//
	// Cap longitudinal forearm growth at +35%. That gives useful seated reach
	// without turning the arm into rubber.
	const float naturalReach = cal.upperLen+cal.lowerLen;
	// A locked offhand has to stay on the gun's real support socket. Allow
	// additional forearm reach for that one case rather than leaving the hand
	// at the arm solver's reach limit and visibly detached from the weapon.
	const float maxForearmStretch =
		preserveWeaponContact ? 2.25f : 1.35f;
	const float maxExtraForearm =
		cal.lowerLen*(maxForearmStretch-1.0f);
	const float requestedExtra =
		Max(0.0f,targetDistance-naturalReach);
	const float extraForearm =
		Min(requestedExtra,maxExtraForearm);
	const float solvedLowerLen =
		cal.lowerLen+extraForearm;
	const float forearmStretch =
		cal.lowerLen > 1.0e-5f ?
			solvedLowerLen/cal.lowerLen : 1.0f;

	const float maxReach = cal.upperLen+solvedLowerLen;
	const float minReach =
		Max(0.02f,Abs(cal.upperLen-solvedLowerLen)+0.005f);
	const float d = ClampF(targetDistance,minReach,maxReach);
	const CVector wrist = shoulder+aim*d;
	CVector hint = FromBasisLocal(cal.elbowOffsetLocal, body);
	hint -= aim*DotProduct(hint,aim);
	if(hint.MagnitudeSqr() < 1.0e-7f){
		const CVector side = handIndex == 0 ? CVector(-1.0f,0.0f,0.0f) : CVector(1.0f,0.0f,0.0f);
		hint = FromBasisLocal(side,body);
		hint -= aim*DotProduct(hint,aim);
	}
	hint = Unit(hint,CVector(0.0f,0.0f,1.0f));
	const float aa=cal.upperLen, cc=solvedLowerLen;
	const float along=ClampF((aa*aa-cc*cc+d*d)/(2.0f*d),0.0f,aa);
	const CVector elbow=shoulder+aim*along+hint*sqrtf(Max(0.0f,aa*aa-along*along));
	const CVector refUpper=FromBasisLocal(cal.upperDirLocal,body);
	const CVector refLower=FromBasisLocal(cal.lowerDirLocal,body);
	const CVector newUpper=elbow-shoulder;
	const CVector newLower=wrist-elbow;
	const CVector uR=FromBasisLocal(cal.upperRightLocal,body), uU=FromBasisLocal(cal.upperUpLocal,body), uA=FromBasisLocal(cal.upperAtLocal,body);
	upper.right=SwingVector(refUpper,newUpper,uR,uU);
	upper.up=SwingVector(refUpper,newUpper,uU,uR);
	upper.at=SwingVector(refUpper,newUpper,uA,uR);
	upper.pos=shoulder;
	Orthonormalise(upper);
	const CVector fR=FromBasisLocal(cal.foreRightLocal,body), fU=FromBasisLocal(cal.foreUpLocal,body), fA=FromBasisLocal(cal.foreAtLocal,body);
	forearm.right=SwingVector(refLower,newLower,fR,fU);
	forearm.up=SwingVector(refLower,newLower,fU,fR);
	forearm.at=SwingVector(refLower,newLower,fA,fR);
	forearm.pos=elbow;
	Orthonormalise(forearm);

	// Grow only the forearm's longitudinal component. Do this AFTER the normal
	// orientation has been orthonormalized; do not orthonormalize again or the
	// intentional length scale would be erased.
	StretchBoneAlongWorldDirection(
		forearm,newLower,forearmStretch);

	hand.right=tracked.GetRight();
	hand.up=tracked.GetUp();
	hand.at=tracked.GetForward();

	// The hand itself is never used as a stretch segment. Ordinary tracked
	// hands stay at the solved wrist if they exceed the normal reach cap. A
	// locked weapon support hand prioritizes the authoritative gun socket; the
	// larger forearm allowance above minimizes any gap while preserving contact.
	hand.pos =
		(preserveWeaponContact ||
		 targetDistance <= maxReach+0.0001f) ?
			tracked.GetPosition() : wrist;
	Orthonormalise(hand);

	// Weapon-holding IK pose: pitch the ACTUAL visible hand around the
	// physical across-palm axis from the raw tracked presentation pose.
	// The skinned hand's own matrix axes are model/bone axes, which is why
	// the previous 35-degree rotation produced essentially no useful pitch.
	if(pitchWeaponHand && physicalPitchAxis.MagnitudeSqr() > 1.0e-8f){
		// The two skinned hands are mirrored. Applying the same signed rotation
		// to both makes the left hand turn inward past the weapon while the
		// right hand seats correctly. Mirror the sign by hand.
		const float weaponHandPitchDeg =
			(handIndex == 0) ? 35.0f : -35.0f;
		RotateHandBasis(
			hand, physicalPitchAxis,
			DEGTORAD(weaponHandPitchDeg));
	}

	ApplyStaticGripHandPose(
		hier, mats, b.hand, handIndex, parentOf,
		physicalPitchAxis, gripAmount);
	return true;
}
}

void Reset(void)
{
	gWrist[0] = WristCalibration();
	gWrist[1] = WristCalibration();
	gShoulder[0] = ShoulderAnchor();
	gShoulder[1] = ShoulderAnchor();
	gUpperBody = UpperBodyAnchor();
	gArmSolve[0] = ArmSolveAnchor();
	gArmSolve[1] = ArmSolveAnchor();
	gRigidWeaponLink[0] = RigidWeaponHandLink();
	gRigidWeaponLink[1] = RigidWeaponHandLink();
	gSolvedHandValid[0] = false;
	gSolvedHandValid[1] = false;
	gVehicleNeckValid = false;
	gVehicleNeckVehicle = nil;
	gBikeBodyVehicle = nil;
	gBikeBodyRootValid = false;
	gBikeBodyLateralValid = false;
	gBikeBodyLateral = 0.0f;
	gBikeBodyLateralLast = 0.0f;
	gBikeBodyLateralAccum = 0.0f;
	gBikeBodyLateralStableFrames = 0;
	gHandCarrierPed = nil;
	gHandCarrierValid = false;
	gHandCarrierWasVehicle = false;
	gHandPose[0] = HandDescendantPose();
	gHandPose[1] = HandDescendantPose();
	gLastPlayer = nil;
}

void ResetVehicleTransition(void)
{
	// Preserve ONLY the anatomical measurements learned from the clean on-foot
	// skeleton (gShoulder/gUpperBody/gArmSolve and static hand descendants).
	//
	// Everything below is render/context state and MUST NOT cross a
	// vehicle <-> on-foot ownership boundary. Leaving any of it alive can make
	// the first on-foot solve consume a vehicle carrier, bike seat, solved wrist
	// or vehicle weapon relationship.
	for(int i = 0; i < 2; i++){
		gWrist[i].frozen = false;
		gWrist[i].clickDown = false;
		gWrist[i].lastPoseValid = false;
		gSolvedHandValid[i] = false;
		gRigidWeaponLink[i] = RigidWeaponHandLink();
	}

	gVehicleNeckValid = false;
	gVehicleNeckVehicle = nil;

	gBikeBodyVehicle = nil;
	gBikeBodyRootValid = false;
	gBikeBodyLateralValid = false;
	gBikeBodyLateral = 0.0f;
	gBikeBodyLateralLast = 0.0f;
	gBikeBodyLateralAccum = 0.0f;
	gBikeBodyLateralStableFrames = 0;

	gHandCarrierPed = nil;
	gHandCarrierValid = false;
	gHandCarrierWasVehicle = false;

	// Do NOT call Reset(): that would throw away the known-good anatomical
	// anchors and re-learn them from a vehicle enter/exit animation pose.
	IKCALLOG("[IK] vehicle transition: transient ownership cleared; anatomy preserved");
}

static void EnsureBakedCalibration(void)
{
	// Measured in-headset with the two-press wrist calibration tool.
	// These are controller-local hand transforms only; weapon transforms are untouched.
	if(!gWrist[0].valid){
		gWrist[0].valid = true;
		gWrist[0].positionLocal = CVector(-0.034706f, -0.005371f, -0.002082f);
		gWrist[0].rightLocal = CVector(0.255107f, -0.086611f, 0.963026f);
		gWrist[0].upLocal = CVector(-0.047421f, -0.995902f, -0.077006f);
		gWrist[0].atLocal = CVector(-0.965749f, 0.026023f, 0.258169f);
	}
	if(!gWrist[1].valid){
		gWrist[1].valid = true;
		gWrist[1].positionLocal = CVector(-0.016873f, -0.013284f, 0.000948f);
		gWrist[1].rightLocal = CVector(0.006246f, 0.394067f, 0.919061f);
		gWrist[1].upLocal = CVector(-0.124347f, -0.911639f, 0.391730f);
		gWrist[1].atLocal = CVector(0.992219f, -0.116729f, 0.043307f);
	}
}

bool
GetSolvedHandWorldBasis(int hand, CVector *position, CVector *right,
	CVector *up, CVector *forward)
{
#if !defined(__ANDROID__) || !defined(GTA_VR_WEAPONS)
	(void)hand;
	(void)position;
	(void)right;
	(void)up;
	(void)forward;
	return false;
#else
	if(hand < 0 || hand > 1 || !position || !right || !up || !forward ||
	   !WeaponOwnershipActive() || !gSolvedHandValid[hand])
		return false;

	const CMatrix &wrist = gSolvedHandWorld[hand];
	*position = wrist.GetPosition();
	*right = wrist.GetRight();
	*up = wrist.GetUp();
	*forward = wrist.GetForward();

	if(right->MagnitudeSqr() <= 1.0e-8f ||
	   up->MagnitudeSqr() <= 1.0e-8f ||
	   forward->MagnitudeSqr() <= 1.0e-8f)
		return false;
	right->Normalise();
	up->Normalise();
	forward->Normalise();
	return true;
#endif
}

bool
GetVehicleNeckWorld(CPed *ped, CVector *position)
{
#if !defined(__ANDROID__) || !defined(GTA_VR_WEAPONS)
	(void)ped;
	(void)position;
	return false;
#else
	if(!position || ped == nil || !VehicleEnabled() ||
	   !ped->InVehicle() || ped->m_pMyVehicle == nil ||
	   !ped->m_pMyVehicle->IsBike() ||
	   !gVehicleNeckValid ||
	   gVehicleNeckVehicle != ped->m_pMyVehicle)
		return false;

	// This is the exact final world-space neck position written into the same
	// transformed skin matrix array that will be rendered this frame.
	*position = gVehicleNeckWorld;
	return true;
#endif
}

bool
GetRigidWeaponHandBasis(int hand, CVector *position, CVector *right,
	CVector *up, CVector *forward)
{
#if !defined(__ANDROID__) || !defined(GTA_VR_WEAPONS)
	(void)hand;
	(void)position;
	(void)right;
	(void)up;
	(void)forward;
	return false;
#else
	if(!WeaponOwnershipActive())
		return false;
	return RebuildRigidWeaponBasis(hand, position, right, up, forward);
#endif
}

void Apply(CPed *ped)
{
#if !defined(__ANDROID__) || !defined(GTA_VR_WEAPONS)
	(void)ped;
	return;
#else
	if(ped == nil || !ped->IsPlayer() || VrRagdoll::Owns(ped))
		return;

	// Cinematic stereo still sets gVrFirstPersonActive because it uses the VR
	// eye renderer. Never mistake that for gameplay. Cutscenes keep their
	// authored native skeleton and the camera follows that animation directly.
	if(!BodyIKGameplayContext() || gVehicleTransitionActive){
		// Preserve the native full-body sequence, including arm reach, door pull,
		// and occupant drag. These cached transforms must not survive the yield.
		gSolvedHandValid[0] = false;
		gSolvedHandValid[1] = false;
		gRigidWeaponLink[0] = RigidWeaponHandLink();
		gRigidWeaponLink[1] = RigidWeaponHandLink();
		gHandCarrierPed = nil;
		gHandCarrierValid = false;
		return;
	}

	const bool onFootIK =
		Enabled() && OculusVR::IsQuestFirstPersonOnFoot();
	const bool vehicleIK =
		VehicleEnabled() && ::gVrFirstPersonActive &&
		ped->InVehicle() && ped->m_pMyVehicle != nil;
	if(!onFootIK && !vehicleIK)
		return;

	if(gLastPlayer != ped){
		Reset();
		gLastPlayer = ped;
	}

	EnsureBakedCalibration();

	gSolvedHandValid[0] = false;
	gSolvedHandValid[1] = false;

	RpHAnimHierarchy *hier =
		GetAnimHierarchyFromSkinClump(ped->GetClump());
	if(hier == nil)
		return;
	RwMatrix *mats = RpHAnimHierarchyGetMatrixArray(hier);
	if(mats == nil)
		return;

	UpperBodyBones upperBody;
	if(!ResolveUpperBody(hier, upperBody))
		return;

	ArmBones arms[2];
	if(!ResolveArm(hier, true, arms[0]) ||
	   !ResolveArm(hier, false, arms[1]))
		return;

	// Anatomy is learned only from the untouched native skeleton.
	if(!CaptureArmSolveAnchor(ped, mats, arms[0], 0) ||
	   !CaptureArmSolveAnchor(ped, mats, arms[1], 1))
		return;

	// On foot, keep the established grounded torso/shoulder stabilization.
	//
	// In vehicles, preserve the native seated ANIMATION. On motorcycles with
	// Sitting Body IK, additionally rotate that entire seated skeleton into
	// the motorcycle's complete world basis. This makes pelvis, torso, neck,
	// shoulders and legs lean/pitch/roll WITH the bike instead of remaining in
	// the ped's flatter native world frame while the camera follows the bike.
	CMatrix visualBody = ped->GetMatrix();
	if(!vehicleIK){
		if(!BuildGroundedVisualBody(ped, hier, mats, visualBody))
			return;

		if(!ped->bIsDucking){
			// Standing/walking keeps the established stable torso/neck/shoulder
			// anchors so locomotion bob never reaches the HMD/body.
			float authoredVerticalOffset = 0.0f;
			if(!StabilizeUpperBody(
			   ped, mats, upperBody, visualBody, &authoredVerticalOffset))
				return;

			StabilizeShoulderRoot(
				ped, mats, arms[0], 0, visualBody, authoredVerticalOffset);
			StabilizeShoulderRoot(
				ped, mats, arms[1], 1, visualBody, authoredVerticalOffset);
		}else{
			// Crouching is an explicit gameplay state. Preserve the complete
			// native crouch spine/neck/clavicle/shoulder pose. The grounded VR
			// body carrier above has already moved that crouched skeleton into
			// the stable roomscale/body frame; SolveArm below only replaces the
			// arm chains from the real crouched shoulders to the tracked hands.
		}
	}else if(ped->m_pMyVehicle != nil &&
	         ped->m_pMyVehicle->IsBike()){
		const CMatrix nativeBody = ped->GetMatrix();
		const CVector nativeRoot = ped->GetPosition();
		CVehicle *bike = ped->m_pMyVehicle;

		// The native seated ped root can wobble relative to the bike from
		// animation/vehicle simulation. Latch its seat location ONCE in bike
		// space and rebuild the carrier root from the bike every frame.
		if(gBikeBodyVehicle != bike){
			gBikeBodyVehicle = bike;
			gBikeBodyRootValid = false;
			gBikeBodyLateralValid = false;
			gBikeBodyLateral = 0.0f;
			gBikeBodyLateralLast = 0.0f;
			gBikeBodyLateralAccum = 0.0f;
			gBikeBodyLateralStableFrames = 0;
		}

		if(!gBikeBodyRootValid){
			const CVector rootDelta = nativeRoot-bike->GetPosition();
			gBikeBodyRootLocal = CVector(
				DotProduct(rootDelta, bike->GetRight()),
				DotProduct(rootDelta, bike->GetUp()),
				DotProduct(rootDelta, bike->GetForward()));
			gBikeBodyRootValid = true;
		}

		const CVector currentRootDelta =
			nativeRoot-bike->GetPosition();
		const float currentLateral =
			DotProduct(currentRootDelta, bike->GetRight());

		// Settle-and-latch lateral center:
		// - never trust the first mount frame
		// - never follow the noisy native lateral root forever
		// - require 12 consecutive near-identical seated samples
		// - latch their average once stable
		if(!gBikeBodyLateralValid){
			const float lateralDelta =
				Abs(currentLateral-gBikeBodyLateralLast);

			// Ignore obviously transitional/mount samples far from the bike.
			if(Abs(currentLateral) < 0.60f &&
			   (gBikeBodyLateralStableFrames == 0 ||
			    lateralDelta < 0.015f)){
				if(gBikeBodyLateralStableFrames == 0)
					gBikeBodyLateralAccum = 0.0f;

				gBikeBodyLateralAccum += currentLateral;
				gBikeBodyLateralStableFrames++;
				gBikeBodyLateralLast = currentLateral;

				if(gBikeBodyLateralStableFrames >= 12){
					gBikeBodyLateral =
						gBikeBodyLateralAccum /
						(float)gBikeBodyLateralStableFrames;
					gBikeBodyLateralValid = true;
				}
			}else{
				gBikeBodyLateralStableFrames = 0;
				gBikeBodyLateralAccum = 0.0f;
				gBikeBodyLateralLast = currentLateral;
			}
		}

		// Before the seated center has settled, follow native lateral placement
		// only during the brief mount transition. Once latched, the entire body
		// uses a fixed motorcycle-local lateral center and cannot shake side to
		// side with the native ped root.
		const float stableLateral =
			gBikeBodyLateralValid ?
				gBikeBodyLateral : currentLateral;

		const CVector stableRoot =
			bike->GetPosition()+
			bike->GetRight()*stableLateral+
			bike->GetUp()*gBikeBodyRootLocal.y+
			bike->GetForward()*gBikeBodyRootLocal.z;

		visualBody = nativeBody;
		visualBody.GetPosition() = stableRoot;

		// One authoritative motorcycle carrier basis is used for the whole body.
		if(BuildBikeCarrierBasis(bike, nativeBody, visualBody)){

			// Preserve the native seated animation SHAPE relative to its current
			// native root, but carry that shape from the stable bike-latched root.
			for(int i = 0; i < hier->numNodes; i++){
				const CVector localPos =
					ToBasisLocal(CVector(mats[i].pos)-nativeRoot,
					             nativeBody);
				mats[i].pos =
					stableRoot + FromBasisLocal(localPos, visualBody);

				const CVector localRight =
					ToBasisLocal(CVector(mats[i].right), nativeBody);
				const CVector localUp =
					ToBasisLocal(CVector(mats[i].up), nativeBody);
				const CVector localAt =
					ToBasisLocal(CVector(mats[i].at), nativeBody);

				mats[i].right =
					FromBasisLocal(localRight, visualBody);
				mats[i].up =
					FromBasisLocal(localUp, visualBody);
				mats[i].at =
					FromBasisLocal(localAt, visualBody);
				Orthonormalise(mats[i]);
			}

			// Publish the ACTUAL transformed neck from the same bone array the
			// renderer will draw. The camera never re-derives this from a
			// separately latched PED_NECK transform again.
			gVehicleNeckWorld =
				CVector(mats[upperBody.neck].pos);
			gVehicleNeckVehicle = bike;
			gVehicleNeckValid = true;
		}
	}else{
		gVehicleNeckValid = false;
		gVehicleNeckVehicle = nil;
		gBikeBodyVehicle = nil;
		gBikeBodyRootValid = false;
		gBikeBodyLateralValid = false;
		gBikeBodyLateralStableFrames = 0;
		gBikeBodyLateralAccum = 0.0f;
	}

	// SAME-FRAME BODY/HAND SYNCHRONIZATION:
	//
	// Body carrier, upper body and shoulder roots are already finalized for
	// THIS render frame before we refresh/capture the tracked wrists.
	// This path is shared by on-foot and vehicle IK.
	//
	// There is no previous-frame carrier correction and no extra rotation of
	// the tracked controller basis.
	OculusVR::RefreshQuestTrackedHandWorldPosesForRender();

	CMatrix desiredWorld[2];
	bool desiredValid[2] = { false, false };
	bool weaponSupportLocked[2] = { false, false };
	CVector physicalPitchAxis[2];
	bool physicalPitchAxisValid[2] = { false, false };

	const androidgame::PadInput &ikInput = androidgame::GetPadInput();
	const float handGrip[2] = {
		ClampF(ikInput.leftGrip, 0.0f, 1.0f),
		ClampF(ikInput.rightGrip, 0.0f, 1.0f)
	};

	for(int handIndex = 0; handIndex < 2; handIndex++){
		WristCalibration &cal = gWrist[handIndex];

		CMatrix raw;
		if(vehicleIK){
			if(!GetVehicleHandPresentationPose(handIndex, raw))
				continue;
		}else if(!GetFloatingHandPresentationPose(handIndex, raw))
			continue;

		// Floating hands already use the weapon's support socket, but Body IK
		// previously skipped that lock and solved the offhand toward the raw
		// controller position. Apply the same support-hand lock BEFORE solving
		// the arm so the skinned wrist receives the exact same target as the
		// floating hand. Only the active support hand is changed.
		weaponSupportLocked[handIndex] =
			OculusVR::ApplyTrackedWeaponSupportHandLock(
				handIndex, &raw);

		// Capture the physical across-palm / weapon lateral axis AFTER the
		// two-hand constraint is applied. For the primary wrist this axis must
		// rotate with the weapon, otherwise the fixed hand-pitch step below would
		// reintroduce the unrotated controller frame and break gun/offhand contact.
		physicalPitchAxis[handIndex] = raw.GetRight();
		if(physicalPitchAxis[handIndex].MagnitudeSqr() > 1.0e-8f){
			physicalPitchAxis[handIndex].Normalise();
			physicalPitchAxisValid[handIndex] = true;
		}

		// Runtime L3/R3 hand calibration has been removed. Body IK always uses
		// the baked headset-measured wrist calibration while enabled. Keep its
		// model-specific ORIENTATION calibration, but never apply its controller-
		// local POSITION offset to a locked weapon socket: that was moving the
		// offhand away from the actual support grip.
		if(cal.valid)
			desiredWorld[handIndex] = ApplyCalibration(raw, cal);
		else
			desiredWorld[handIndex] =
				BuildDefaultHandPose(handIndex, raw);
		if(weaponSupportLocked[handIndex])
			desiredWorld[handIndex].GetPosition() = raw.GetPosition();

		CaptureRigidWeaponLink(
			handIndex, desiredWorld[handIndex], raw);


		cal.lastPose = desiredWorld[handIndex];
		cal.lastPoseValid = true;
		desiredValid[handIndex] = true;
	}

	// SAME-CARRIER WRIST POSITION:
	//
	// Do not rotate the wrist basis here. Quest tracking already owns hand
	// orientation. Only advance wrist POSITION from the previous body carrier
	// into the current one, so locomotion/vehicle motion affects shoulder and
	// wrist on the same render timing.
	const bool carrierModeVehicle = vehicleIK;
	const bool sameCarrierContext =
		gHandCarrierValid &&
		gHandCarrierPed == ped &&
		gHandCarrierWasVehicle == carrierModeVehicle;

	if(sameCarrierContext &&
	   CarrierDeltaIsReasonable(gHandCarrierPrev, visualBody)){
		for(int handIndex = 0; handIndex < 2; handIndex++){
			if(!desiredValid[handIndex])
				continue;

			// Weapon support sockets and grabbed steering hands are already
			// positioned from CURRENT authoritative targets. Advancing either
			// through the body carrier again double-applies motion and moves the
			// hand away from the actual grip point.
			if(weaponSupportLocked[handIndex])
				continue;
			if(vehicleIK &&
			   OculusVR::IsImmersiveSteeringHandleGrabbed(handIndex))
				continue;

			desiredWorld[handIndex].GetPosition() =
				AdvancePositionBetweenCarriers(
					desiredWorld[handIndex].GetPosition(),
					gHandCarrierPrev, visualBody);
		}
	}

	// Cache exactly the carrier used by THIS frame's shoulders. If context
	// changes (on-foot <-> vehicle) or a teleport/jump occurs, the first frame
	// simply establishes a fresh baseline with no compensation.
	gHandCarrierPrev = visualBody;
	gHandCarrierPed = ped;
	gHandCarrierWasVehicle = carrierModeVehicle;
	gHandCarrierValid = true;

	if(hier->numNodes <= 0 || hier->numNodes > 96)
		return;
	int parentOf[96];
	BuildParentTable(hier, parentOf);

	// Solve the actual held-weapon hand first. The support socket depends on
	// the primary wrist's final solved basis; locking both hands from the old
	// frame before either arm is solved leaves the left offhand using a stale
	// socket and can put it far to one side of the gun.
	int solveOrder[2] = { 0, 1 };
	if(OculusVR::GetHeldWeaponSlot(1) >= 0 &&
	   OculusVR::GetHeldWeaponSlot(0) < 0){
		solveOrder[0] = 1;
		solveOrder[1] = 0;
	}
	for(int solveIndex = 0; solveIndex < 2; solveIndex++){
		const int handIndex = solveOrder[solveIndex];
		if(!desiredValid[handIndex])
			continue;

		// Once the primary arm has been solved for this frame, rebuild the
		// support hand's target from the current gun socket. Do not apply a new
		// controller-to-socket rotation here: the current primary wrist already
		// drove the weapon orientation for this frame.
		if(weaponSupportLocked[handIndex] &&
		   OculusVR::GetHeldWeaponSlot(handIndex) < 0){
			CMatrix currentSupportPose;
			if(GetFloatingHandPresentationPose(handIndex, currentSupportPose) &&
			   OculusVR::ApplyTrackedWeaponSupportHandLock(
				   handIndex, &currentSupportPose, true)){
				WristCalibration &cal = gWrist[handIndex];
				if(cal.valid)
					desiredWorld[handIndex] =
						ApplyCalibration(currentSupportPose, cal);
				else
					desiredWorld[handIndex] =
						BuildDefaultHandPose(handIndex, currentSupportPose);
				// The socket is authoritative; wrist calibration changes orientation
				// but must not translate this contact point.
				desiredWorld[handIndex].GetPosition() =
					currentSupportPose.GetPosition();
			}
		}

		// Solve shoulder/elbow toward the frozen world wrist, then write that
		// exact wrist transform back as the final hand matrix. Locomotion and
		// body yaw may move the arm roots, but never the wrist target.
		const bool pitchWeaponHand =
			physicalPitchAxisValid[handIndex] &&
			OculusVR::GetHeldWeaponSlot(handIndex) >= 0;

		if(SolveArm(hier, mats, parentOf,
		   arms[handIndex], handIndex,
		   desiredWorld[handIndex], visualBody,
		   physicalPitchAxis[handIndex], pitchWeaponHand,
		   handGrip[handIndex], weaponSupportLocked[handIndex])){
			// Weapon ownership must use the ACTUAL final skinned wrist matrix,
			// including the weapon-holding hand pitch applied inside SolveArm().
			RwMatrix &finalHand = mats[arms[handIndex].hand];
			gSolvedHandWorld[handIndex].GetPosition() =
				CVector(finalHand.pos);
			gSolvedHandWorld[handIndex].GetRight() =
				CVector(finalHand.right);
			gSolvedHandWorld[handIndex].GetUp() =
				CVector(finalHand.up);
			gSolvedHandWorld[handIndex].GetForward() =
				CVector(finalHand.at);
			OrthonormaliseBasis(gSolvedHandWorld[handIndex]);
			gSolvedHandValid[handIndex] = true;
		}else{
			gSolvedHandValid[handIndex] = false;
		}
	}
#endif
}
}
