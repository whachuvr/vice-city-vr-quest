#pragma once

class CPed;
class CVector;

// Quest player-body IK. This module is presentation-only: it consumes the
// already-final tracked visual hand poses and rewrites player skin bones after
// native animation. It never owns or modifies weapon transforms, aim rays,
// support grips, holsters, firing, or interaction anchors.
namespace VrBodyIK
{
bool Enabled(void);
bool VehicleEnabled(void);
// True while the native enter/exit/carjack animation owns Tommy's upper body.
void SetVehicleTransition(bool active);
bool WeaponOwnershipActive(void);
void Apply(CPed *ped);
void Reset(void);
void ResetVehicleTransition(void);
bool GetRigidWeaponHandBasis(int hand, CVector *position, CVector *right,
	CVector *up, CVector *forward);
bool GetSolvedHandWorldBasis(int hand, CVector *position, CVector *right,
	CVector *up, CVector *forward);
bool GetVehicleNeckWorld(CPed *ped, CVector *position);
}
