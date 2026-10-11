# Assembles a buildable source tree for the MiamiVR Quest port.
#
#   .\tools\assemble.ps1 -Revc <path to reVC source> -Out <output dir>
#
# The reVC source tree is not part of this repository and is not distributed
# with it. Obtain the miami branch separately; the tested patch base is commit
# 026cd10f3fdbd92c089830e5067c4457c53c1b51 from mrxenginner/reVC.
param(
    [Parameter(Mandatory=$true)][string]$Revc,
    [Parameter(Mandatory=$true)][string]$Out
)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent

if (-not (Test-Path (Join-Path $Revc "src\core\main.cpp"))) {
    Write-Error "'$Revc' does not look like a reVC source tree (src\core\main.cpp not found)"
}
if (Test-Path $Out) { Write-Error "'$Out' already exists; refusing to overwrite" }

Write-Host "[1/4] Copying the reVC tree..."
New-Item -ItemType Directory -Path $Out | Out-Null
robocopy $Revc $Out /E /NFL /NDL /NJH /NJS /XD ".git" | Out-Null
if ($LASTEXITCODE -ge 8) {
    Write-Error "Failed to copy reVC (robocopy exit code $LASTEXITCODE). Remove the partial output and retry."
}

Write-Host "[2/4] Applying the port patch..."
Push-Location $Out
git init -q 2>$null
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-public-base-compat.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "Public reVC compatibility patch did not apply. The source must be a clean checkout of commit 026cd10f3fdbd92c089830e5067c4457c53c1b51 with no local edits and no patches already applied."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "Patch did not apply after public-base compatibility."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-v050.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The v0.5.0 parity patch did not apply. Make sure the base Quest patch applied cleanly."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-v050-integration.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The v0.5.0 integration patch did not apply after the parity patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v2.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v2 patch did not apply after the v0.5.0 integration patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v3.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v3 patch did not apply after the runtime v2 patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v4.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v4 patch did not apply after the runtime v3 patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v5.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v5 patch did not apply after the runtime v4 patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v6.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v6 patch did not apply after the runtime v5 patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v7.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v7 patch did not apply after the runtime v6 patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v8.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v8 patch did not apply after the runtime v7 patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v9.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v9 patch did not apply after the runtime v8 patch."
}
git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v10.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v10 patch did not apply after the runtime v9 patch."
}

git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v11.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v11 patch did not apply after the runtime v10 patch."
}

git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v12.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v12 patch did not apply after the runtime v11 patch."
}

git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v13.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v13 patch did not apply after the runtime v12 patch."
}

git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v14.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v14 patch did not apply after the runtime v13 patch."
}

git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v15.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v15 patch did not apply after the runtime v14 patch."
}

git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v16.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v16 patch did not apply after the runtime v15 patch."
}

git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v17.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v17 patch did not apply after the runtime v16 patch."
}

git apply --whitespace=nowarn (Join-Path $repo "patches\revc-quest-runtime-v18.patch")
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "The runtime v18 patch did not apply after the runtime v17 patch."
}

# Body IK is only two presentation hooks in Ped.cpp. Apply them by exact,
# validated anchors instead of making the entire clean assembly depend on a
# fragile line-number/context patch. This does not touch weapon transforms.
$pedPath = Join-Path $Out "src\peds\Ped.cpp"
$pedText = [System.IO.File]::ReadAllText($pedPath)

if ($pedText -notmatch '(?m)^#include "VrBodyIK\.h"\s*$') {
    $includeAnchor = '#include "Population.h"' + [Environment]::NewLine + '#include "VrRagdoll.h"'
    if (-not $pedText.Contains($includeAnchor)) {
        # Accept LF-only copied trees as well.
        $includeAnchor = '#include "Population.h"' + "`n" + '#include "VrRagdoll.h"'
    }
    if (-not $pedText.Contains($includeAnchor)) {
        Pop-Location
        Write-Error "Body IK integration failed: expected Population.h/VrRagdoll.h include anchor was not found after runtime v18."
    }
    $includeReplacement = '#include "Population.h"' + ($includeAnchor -replace '^#include "Population\.h"','')
    $includeReplacement = $includeReplacement -replace '#include "VrRagdoll\.h"', ('#include "VrBodyIK.h"' + [Environment]::NewLine + '#include "VrRagdoll.h"')
    # Preserve LF-only trees if needed.
    if ($includeAnchor.Contains("`n") -and -not $includeAnchor.Contains("`r`n")) {
        $includeReplacement = $includeReplacement -replace "`r`n", "`n"
    }
    $pedText = $pedText.Replace($includeAnchor, $includeReplacement)
}

if ($pedText -notmatch '(?m)^\s*VrBodyIK::Apply\(this\);\s*$') {
    $applyAnchor = "`tVrRagdoll::Apply(this);"
    if (-not $pedText.Contains($applyAnchor)) {
        Pop-Location
        Write-Error "Body IK integration failed: expected VrRagdoll::Apply(this) anchor was not found after runtime v18."
    }
    $nl = if ($pedText.Contains("`r`n")) { "`r`n" } else { "`n" }
    $applyReplacement = $applyAnchor + $nl +
        "`t// Presentation-only player IK runs after native animation. It consumes" + $nl +
        "`t// tracked visual hand poses but never writes weapon transforms or aim state." + $nl +
        "`tVrBodyIK::Apply(this);"
    $pedText = $pedText.Replace($applyAnchor, $applyReplacement)
}

# Body visibility for on-foot IK.
#
# gVrHidePlayerBody is now false on foot in overlay/src/skel/android/android.cpp,
# so the existing renderer-level and early CPed::Render gates naturally allow
# Tommy through. Do NOT rewrite those gates here; they are already keyed to the
# shared flag and keeping them untouched avoids fragile patch coupling.
#
# One later CPed::Render policy independently hides the player's CEntity body
# whenever tracked hands are active. Restrict only that policy to immersive
# vehicle driving. Use regex so whitespace/line-ending changes in the patched
# reVC source cannot break clean assembly.
$renderPolicyPattern = '(?ms)hideVrPlayerBody\s*=\s*IsPlayer\(\)\s*&&\s*OculusVR::ShouldUseTrackedHands\(\)\s*&&\s*\(\s*!bInVehicle\s*\|\|\s*OculusVR::IsImmersiveDrivingActive\(\)\s*\)\s*;'
$renderPolicyReplacement = @'
hideVrPlayerBody = IsPlayer() && OculusVR::ShouldUseTrackedHands() &&
		((!bInVehicle && !VrBodyIK::Enabled()) ||
		 (bInVehicle && OculusVR::IsImmersiveDrivingActive() &&
		  !VrBodyIK::VehicleEnabled()));
'@
$renderMatches = [regex]::Matches($pedText, $renderPolicyPattern)
if ($renderMatches.Count -eq 1) {
    $pedText = [regex]::Replace($pedText, $renderPolicyPattern, $renderPolicyReplacement, 1)
} elseif ($pedText -notmatch '(?ms)hideVrPlayerBody\s*=\s*IsPlayer\(\)\s*&&\s*OculusVR::ShouldUseTrackedHands\(\)\s*&&\s*\(\s*\(\s*!bInVehicle\s*&&\s*!VrBodyIK::Enabled\(\)\s*\)\s*\|\|\s*\(\s*bInVehicle\s*&&\s*OculusVR::IsImmersiveDrivingActive\(\)\s*\)\s*\)\s*;') {
    Pop-Location
    Write-Error "Body IK visibility integration failed: the independent tracked-body render policy was not found."
}

# With Tommy visible on foot, suppress only his head while the first-person HMD
# is inside it. The runtime-v8 source already has this exact policy for default
# first-person driving; broaden that condition to every visible first-person
# player-body state. Again, match only the condition fragment rather than an
# indentation-sensitive block.
$headPolicyPattern = 'gVrFirstPersonActive\s*&&\s*gVrInVehicle\s*&&\s*!gVrHidePlayerBody'
$headMatches = [regex]::Matches($pedText, $headPolicyPattern)
if ($headMatches.Count -eq 1) {
    $pedText = [regex]::Replace(
        $pedText,
        $headPolicyPattern,
        'gVrFirstPersonActive && !gVrHidePlayerBody',
        1
    )
} elseif ($pedText -notmatch 'gVrFirstPersonActive\s*&&\s*!gVrHidePlayerBody') {
    Pop-Location
    Write-Error "Body IK visibility integration failed: the first-person head suppression policy was not found."
}


# Some stereo cutscenes use a live CPed Tommy rather than CCutsceneObject.
# runtime-v8's CPed path only zero-scales PED_HEAD, which leaves animated
# jaw/eye/lip-sync descendants alive. That produces visible eyes, lower jaw
# and long talking skin spikes even though CCutsceneObject cutscenes are clean.
#
# Preserve the existing single-head behavior for normal gameplay. Only when
# THIS ped is the active first-person cutscene camera actor, replace the
# one-bone collapse with a full HAnim subtree collapse rooted at BONE_head.
$cutPedHeadPattern = '(?ms)(if\s*\(\(gVrFirstPersonActive\s*&&\s*!gVrHidePlayerBody\s*&&\s*\(CEntity\*\)this\s*==\s*gVrPlayerEntity\)\s*\|\|\s*androidgame::VrCutsceneCameraActor\(\)\s*==\s*\(void\*\)this\)\s*\{\s*RpHAnimHierarchy\*\s*hier\s*=\s*GetAnimHierarchyFromSkinClump\(GetClump\(\)\);\s*int32\s+idx\s*=\s*RpHAnimIDGetIndex\(hier,\s*ConvertPedNode2BoneTag\(PED_HEAD\)\);\s*RwMatrix\*\s*head\s*=\s*&RpHAnimHierarchyGetMatrixArray\(hier\)\[idx\];\s*RwV3d\s+zero\s*=\s*\{\s*0\.0f,\s*0\.0f,\s*0\.0f\s*\};\s*RwMatrixScale\(head,\s*&zero,\s*rwCOMBINEPRECONCAT\);\s*\})'

$cutPedHeadReplacement = @'
if ((gVrFirstPersonActive && !gVrHidePlayerBody &&
     (CEntity*)this == gVrPlayerEntity) ||
    androidgame::VrCutsceneCameraActor() == (void*)this) {
	RpHAnimHierarchy* hier = GetAnimHierarchyFromSkinClump(GetClump());
	RwMatrix* matrices = hier ? RpHAnimHierarchyGetMatrixArray(hier) : nil;
	if (hier && matrices) {
		RwV3d zero = { 0.0f, 0.0f, 0.0f };

		if (androidgame::VrCutsceneCameraActor() == (void*)this) {
			// Cutscene CPed: collapse the COMPLETE head subtree to the neck so animated
			// eyes, jaw and lip-sync children cannot survive PED_HEAD.
			enum { MAX_VR_PED_BONES = 128 };
			if (hier->numNodes > 0 && hier->numNodes <= MAX_VR_PED_BONES) {
				bool inHead[MAX_VR_PED_BONES];
				int parentStack[32];
				int sp = 0;
				int parent = 0;

				for (int node = 0; node < hier->numNodes; node++)
					inHead[node] = false;

				for (int node = 0; node < hier->numNodes; node++) {
					const int flags = hier->nodeInfo[node].flags;

					if (node > 0)
						inHead[node] = inHead[parent];

					if (hier->nodeInfo[node].id == BONE_head)
						inHead[node] = true;

					if (flags & rpHANIMPUSHPARENTMATRIX && sp < 32)
						parentStack[sp++] = parent;

					parent = node;

					if (flags & rpHANIMPOPPARENTMATRIX && sp > 0)
						parent = parentStack[--sp];
				}

				// Find the actual parent of BONE_head. All hidden head/facial
				// descendants will collapse to this ONE neck-space point.
				int neckParent = -1;
				sp = 0;
				parent = 0;
				for (int node = 0; node < hier->numNodes; node++) {
					const int flags = hier->nodeInfo[node].flags;

					if (hier->nodeInfo[node].id == BONE_head) {
						neckParent = parent;
						break;
					}

					if (flags & rpHANIMPUSHPARENTMATRIX && sp < 32)
						parentStack[sp++] = parent;

					parent = node;

					if (flags & rpHANIMPOPPARENTMATRIX && sp > 0)
						parent = parentStack[--sp];
				}

				if (neckParent >= 0 && neckParent < hier->numNodes) {
					const RwMatrix neckMatrix = matrices[neckParent];

					for (int node = 0; node < hier->numNodes; node++) {
						if (!inHead[node])
							continue;

						// Collapse this head/facial contribution completely.
						// IMPORTANT: keep the basis ZERO. Restoring neck right/up/at
						// here would undo the zero scale and make the hidden facial
						// skin full-sized again at the neck.
						RwMatrixScale(&matrices[node], &zero,
							rwCOMBINEPRECONCAT);

						// All hidden head descendants share one neck-space point,
						// while remaining truly zero-sized.
						matrices[node].pos = neckMatrix.pos;
						matrices[node].right.x = 0.0f;
						matrices[node].right.y = 0.0f;
						matrices[node].right.z = 0.0f;
						matrices[node].up.x = 0.0f;
						matrices[node].up.y = 0.0f;
						matrices[node].up.z = 0.0f;
						matrices[node].at.x = 0.0f;
						matrices[node].at.y = 0.0f;
						matrices[node].at.z = 0.0f;
					}
				}
			}
		} else {
			// Normal gameplay: preserve the established one-bone head hide.
			const int32 idx =
				RpHAnimIDGetIndex(hier, ConvertPedNode2BoneTag(PED_HEAD));
			if (idx >= 0)
				RwMatrixScale(&matrices[idx], &zero,
					rwCOMBINEPRECONCAT);
		}
	}
}
'@

$cutPedHeadMatches = [regex]::Matches($pedText, $cutPedHeadPattern)
if ($cutPedHeadMatches.Count -eq 1) {
    $pedText = [regex]::Replace(
        $pedText,
        $cutPedHeadPattern,
        $cutPedHeadReplacement,
        1
    )
} elseif (-not $pedText.Contains(
    'Cutscene CPed: collapse the COMPLETE head subtree')) {
    Pop-Location
    Write-Error "Cutscene CPed full-head suppression failed: expected the runtime-v8 Ped.cpp head block exactly once."
}

if (-not $pedText.Contains(
        'Cutscene CPed: collapse the COMPLETE head subtree') -or
    -not $pedText.Contains('inHead[node] = inHead[parent];') -or
    -not $pedText.Contains('hier->nodeInfo[node].id == BONE_head')) {
    Pop-Location
    Write-Error "Cutscene CPed full-head suppression validation failed after editing Ped.cpp."
}

$hasBodyVisibilityPolicy =
    $pedText -match 'hideVrPlayerBody\s*=\s*IsPlayer\(\)\s*&&\s*OculusVR::ShouldUseTrackedHands\(\)' -and
    $pedText -match '!bInVehicle\s*&&\s*!VrBodyIK::Enabled\(\)' -and
    $pedText -match 'bInVehicle\s*&&\s*OculusVR::IsImmersiveDrivingActive\(\)' -and
    $pedText -match '!VrBodyIK::VehicleEnabled\(\)'

$hasRagdollIntegration =
    $pedText -match 'VrRagdoll::Apply\(this\);'

$hasBodyIKIntegration =
    $pedText -match 'VrBodyIK::Apply\(this\);'

if (-not $hasBodyVisibilityPolicy -or
    -not $hasRagdollIntegration -or
    -not $hasBodyIKIntegration) {
    Write-Error "Body IK integration validation failed after editing Ped.cpp."
}
[System.IO.File]::WriteAllText($pedPath, $pedText, [System.Text.UTF8Encoding]::new($false))

# Quest L3 crouch: patch the single native CPad::DuckJustDown() choke point.
# The raw physical edge comes from SetPadInput(); PlayerPed still owns the
# actual SetDuck()/ClearDuck() state and animation.
$padPath = Join-Path $Out "src\core\Pad.cpp"
$padText = [System.IO.File]::ReadAllText($padPath)

# Pad.cpp does not reliably include the Android platform header, so give this
# translation unit the one declaration it needs before calling into androidgame.
if ($padText -notmatch 'bool\s+VrCrouchJustDown\s*\(\s*void\s*\)\s*;') {
    $padIncludeAnchor = '#include "common.h"'
    if ($padText.Contains($padIncludeAnchor)) {
        $padDecl = @'
#if defined(GTA_VR_WEAPONS) && defined(RW_VULKAN) && defined(__ANDROID__)
namespace androidgame { bool VrCrouchJustDown(void); }
#endif
'@
        $padText = $padText.Replace(
            $padIncludeAnchor,
            $padIncludeAnchor + "`r`n" + $padDecl
        )
    } else {
        Pop-Location
        Write-Error "Quest crouch compile integration failed: Pad.cpp common.h include not found."
    }
}

$crouchPattern = '(?ms)#if defined\(GTA_VR_WEAPONS\) && defined\(RW_VULKAN\) && defined\(__ANDROID__\)\s*// The crouch shares its click.*?if \( OculusVR::IsQuestFirstPersonOnFoot\(\) &&\s*!androidgame::VrStickCrouchEnabled\(\) \)\s*return false;\s*#endif\s*\s*return !!\(NewState\.LeftShock && !OldState\.LeftShock\);'
$crouchReplacement = @'
#if defined(GTA_VR_WEAPONS) && defined(RW_VULKAN) && defined(__ANDROID__)
	// On Quest, use the physical L3 rising edge captured before remapping.
	// Native PlayerPed code still performs SetDuck()/ClearDuck().
	if ( OculusVR::IsQuestFirstPersonOnFoot() )
		return androidgame::VrCrouchJustDown();
#endif

	return !!(NewState.LeftShock && !OldState.LeftShock);
'@

$crouchMatches = [regex]::Matches($padText, $crouchPattern)
if ($crouchMatches.Count -eq 1) {
    $padText = [regex]::Replace($padText, $crouchPattern, $crouchReplacement, 1)
} elseif ($padText -notmatch 'return androidgame::VrCrouchJustDown\(\);') {
    Pop-Location
    Write-Error "Quest crouch integration failed: DuckJustDown runtime-v8 block was not found exactly once."
}

if (-not $padText.Contains('return androidgame::VrCrouchJustDown();')) {
    Pop-Location
    Write-Error "Quest crouch integration validation failed: VrCrouchJustDown call is missing after editing Pad.cpp."
}
[System.IO.File]::WriteAllText($padPath, $padText, [System.Text.UTF8Encoding]::new($false))

# Quest general on-foot crouch.
#
# reVC's stock Vice City code has CPad::DuckJustDown(), but normal locomotion
# does NOT consume it. Only the sniper/M16 special control paths toggle
# SetDuck()/ClearDuck(). That is why routing Quest L3 into DuckJustDown()
# correctly still produced no crouch during ordinary gameplay.
#
# Add the missing native crouch toggle to BOTH normal on-foot locomotion
# controllers. The raw Quest edge still enters through CPad::DuckJustDown();
# all animation/state remains the game's own SetDuck()/ClearDuck().
$playerPedPath = Join-Path $Out "src\peds\PlayerPed.cpp"
$playerPedText = [System.IO.File]::ReadAllText($playerPedPath)

$questCrouchBody = @'
#if defined(GTA_VR_WEAPONS) && defined(RW_VULKAN) && defined(__ANDROID__)
	// Quest adds general on-foot crouching. Stock Vice City only checks
	// DuckJustDown() inside sniper/M16 weapon-control modes.
	if ( padUsed != nil && padUsed->DuckJustDown() ) {
		if ( bIsDucking ) {
			ClearDuck(true);
			bCrouchWhenShooting = false;
		} else if ( !bIsInTheAir &&
		            m_nPedState != PED_JUMP &&
		            m_nMoveState != PEDMOVE_SPRINT &&
		            !InVehicle() ) {
			// General locomotion crouch uses the normal full-body duck path.
			bCrouchWhenShooting = false;
			SetDuck(60000, true);
		}
	}
#endif
'@

$normalControlFunctions = @(
    'PlayerControlZelda',
    'PlayerControl1stPersonRunAround'
)

foreach ($fn in $normalControlFunctions) {
    $marker = "// Quest general crouch: $fn"
    if ($playerPedText.Contains($marker)) {
        continue
    }

    $pattern = "(?ms)(void\s+CPlayerPed::$fn\s*\(\s*CPad\s*\*\s*padUsed\s*\)\s*\{\s*)"
    $matches = [regex]::Matches($playerPedText, $pattern)
    if ($matches.Count -ne 1) {
        Pop-Location
        Write-Error "Quest crouch integration failed: expected exactly one CPlayerPed::$fn(CPad *padUsed), found $($matches.Count)."
    }

    $injected = '$1' + "`r`n`t$marker`r`n" + $questCrouchBody + "`r`n"
    $playerPedText = [regex]::Replace(
        $playerPedText,
        $pattern,
        $injected,
        1
    )
}

foreach ($fn in $normalControlFunctions) {
    if ($playerPedText -notmatch [regex]::Escape("// Quest general crouch: $fn")) {
        Pop-Location
        Write-Error "Quest crouch integration validation failed for $fn."
    }
}

[System.IO.File]::WriteAllText(
    $playerPedPath,
    $playerPedText,
    [System.Text.UTF8Encoding]::new($false)
)
Remove-Item -Recurse -Force (Join-Path $Out ".git")
Pop-Location

Write-Host "[3/4] Copying the port sources..."
robocopy (Join-Path $repo "overlay") $Out /E /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) {
    Write-Error "Failed to copy the Quest overlay (robocopy exit code $LASTEXITCODE)."
}

# Sitting Body IK needs the wheel's ACTUAL steering rotation, not the absolute
# floating-hand socket orientation. Export that tiny piece of authoritative
# driving state from the assembled QuestDrivingVR.cpp without replacing the
# repository's whole driving source file.
$drivingPath = Join-Path $Out "src\vr\QuestDrivingVR.cpp"
$drivingText = [System.IO.File]::ReadAllText($drivingPath)

if ($drivingText -notmatch 'GetQuestVehicleHandSteerRotation') {
    $steerAnchor = @'
bool
GetImmersiveSteeringHandleMatrix(int hand, CMatrix *matrix)
{
	if(IsVrCarActive())
		return BuildCarWheelMatrix(hand, matrix, true);
	return BuildBikeHandleMatrix(hand, matrix, true);
}
'@
    if (-not $drivingText.Contains($steerAnchor)) {
        Pop-Location
        Write-Error "Vehicle Sitting IK integration failed: GetImmersiveSteeringHandleMatrix anchor not found in QuestDrivingVR.cpp."
    }

    $steerReplacement = $steerAnchor + @'

bool
GetQuestVehicleHandSteerRotation(int hand, CVector *axis, float *angle)
{
	if(!axis || !angle || hand < 0 || hand >= VR_HAND_COUNT ||
	   !IsImmersiveSteeringHandleGrabbed(hand))
		return false;

	if(IsImmersiveCarActive()){
		CMatrix handle;
		if(!BuildCarWheelMatrix(hand, &handle, true))
			return false;
		*axis = handle.GetForward();
		if(axis->MagnitudeSqr() < 0.0001f)
			return false;
		axis->Normalise();

		// BuildCarWheelMatrix applies -gImmersiveCarPhysicalAngle around
		// the wheel normal. Return exactly that rotation and nothing else.
		*angle = -gImmersiveCarPhysicalAngle;
		return isfinite(*angle);
	}

	if(IsImmersiveBikeActive()){
		CBike *bike = GetActivePlayerBike();
		BikeHandlePose pose;
		if(!bike || !BuildBikeHandlePose(bike, &pose))
			return false;
		*axis = pose.up;
		if(axis->MagnitudeSqr() < 0.0001f)
			return false;
		axis->Normalise();

		// The bike handle solver applies the physical bar angle around pose.up.
		*angle = gImmersiveBikePhysicalAngle;
		return isfinite(*angle);
	}

	return false;
}
'@

    $drivingText = $drivingText.Replace($steerAnchor, $steerReplacement)
    [System.IO.File]::WriteAllText(
        $drivingPath, $drivingText,
        (New-Object System.Text.UTF8Encoding($false)))
}

$drivingText = [System.IO.File]::ReadAllText($drivingPath)
if ($drivingText -notmatch 'GetQuestVehicleHandSteerRotation' -or
    $drivingText -notmatch '\*angle\s*=\s*-gImmersiveCarPhysicalAngle\s*;' -or
    $drivingText -notmatch '\*angle\s*=\s*gImmersiveBikePhysicalAngle\s*;') {
    Pop-Location
    Write-Error "Vehicle Sitting IK steering-angle export validation failed."
}

# Enforce on-foot hand ownership on the exact assembled source that CMake will
# compile. Runtime patches run before the overlay copy, so doing this here makes
# the policy immune to patch/overlay ordering.
$handsPath = Join-Path $Out "src\skel\android\vrhands_quest.cpp"
$handsText = [System.IO.File]::ReadAllText($handsPath)
if ($handsText -notmatch 'OculusVR::IsQuestFirstPersonOnFoot\(\)') {
    $handsPattern = '(?ms)(void\s+RenderTrackedHands\s*\(\s*void\s*\)\s*\{\s*if\s*\(\s*!::gVrFirstPersonActive\s*\)\s*return\s*;)'
    if ([regex]::Matches($handsText, $handsPattern).Count -ne 1) {
        Write-Error "Body IK hand ownership integration failed: RenderTrackedHands entry was not found."
    }
    $handsText = [regex]::Replace(
        $handsText,
        $handsPattern,
        '$1' + "`r`n`tif(OculusVR::IsQuestFirstPersonOnFoot() && androidgame::VrBodyIKEnabled())`r`n`t`treturn;",
        1
    )
    [System.IO.File]::WriteAllText($handsPath, $handsText, [System.Text.UTF8Encoding]::new($false))
}
# Validate ownership semantically instead of requiring the old exact one-line
# shape. v51 adds vehicle Sitting Body IK to the same guard, so the on-foot
# condition is now nested inside a combined OR expression.
$hasOnFootOwnership =
    $handsText -match 'OculusVR::IsQuestFirstPersonOnFoot\(\)\s*&&\s*androidgame::VrBodyIKEnabled\(\)'
$hasVehicleOwnership =
    $handsText -match 'player->InVehicle\(\)\s*&&\s*androidgame::VrVehicleBodyIKEnabled\(\)'

if (-not $hasOnFootOwnership) {
    Write-Error "Body IK hand ownership validation failed: standalone on-foot hands are not correctly owned by the Body IK toggle."
}
if (-not $hasVehicleOwnership) {
    Write-Error "Vehicle Body IK hand ownership validation failed: standalone vehicle hands are not correctly owned by the Vehicle Body IK toggle."
}

# The base reVC Quest patch has a second, independent immediate-mode hand
# renderer in src/core/main.cpp. It also renders the tracked weapons, lasers,
# holsters and trajectories, so do NOT disable the whole function. Suppress
# only the two custom hand-mesh submissions on foot; all weapon rendering stays
# exactly where it was.
$mainPath = Join-Path $Out "src\core\main.cpp"
$mainText = [System.IO.File]::ReadAllText($mainPath)

if ($mainText -notmatch '(?m)^#include "VrBodyIK\.h"\s*$') {
    $mainIncludePattern = '(?m)^(#include "OculusVR\.h"\s*)$'
    if ([regex]::Matches($mainText, $mainIncludePattern).Count -ne 1) {
        Write-Error "Body IK toggle integration failed: OculusVR.h include was not found in main.cpp."
    }
    $mainText = [regex]::Replace(
        $mainText, $mainIncludePattern,
        '$1' + [Environment]::NewLine + '#include "VrBodyIK.h"', 1)
}

$coreHandsOld = '(?ms)(RwRenderStateSet\(rwRENDERSTATECULLMODE,\s*\(void\*\)rwCULLMODECULLNONE\);\s*)RenderVrTrackedHand\(0\);\s*RenderVrTrackedHand\(1\);'
$coreHandsNew = @'
$1CPlayerPed *vrHandPlayer = FindPlayerPed();
	const bool vrSkinnedHandsOwnPresentation =
		(OculusVR::IsQuestFirstPersonOnFoot() && VrBodyIK::Enabled()) ||
		(VrBodyIK::VehicleEnabled() && vrHandPlayer != nil &&
		 vrHandPlayer->InVehicle());
	if(!vrSkinnedHandsOwnPresentation){
		RenderVrTrackedHand(0);
		RenderVrTrackedHand(1);
	}
'@

if ($mainText -notmatch 'vrSkinnedHandsOwnPresentation') {
    $coreMatches = [regex]::Matches($mainText, $coreHandsOld)
    if ($coreMatches.Count -ne 1) {
        Write-Error "Body IK hand ownership integration failed: core RenderVrTrackedHands hand submissions were not found."
    }
    $mainText = [regex]::Replace($mainText, $coreHandsOld, $coreHandsNew, 1)
    
# Body IK ON: core immediate weapon rendering must use the basis rigidly
# reconstructed from the final solved skinned wrist.
$getVrHandPattern = '(?ms)(static bool\s+GetVrHandBasis\(int hand,\s*CVector &position,\s*CVector &right,\s*CVector &up,\s*CVector &forward\)\s*\{\s*)(CMatrix matrix;)'
if ($mainText -notmatch 'VrBodyIK::GetRigidWeaponHandBasis') {
    $gvhMatches = [regex]::Matches($mainText, $getVrHandPattern)
    if ($gvhMatches.Count -ne 1) {
        Write-Error "Body IK rigid weapon integration failed: GetVrHandBasis was not found."
    }
    $getVrHandReplacement = @'
$1if(VrBodyIK::WeaponOwnershipActive() &&
	   VrBodyIK::GetRigidWeaponHandBasis(
		   hand, &position, &right, &up, &forward))
		return true;
	CMatrix matrix;
'@
    $mainText = [regex]::Replace(
        $mainText, $getVrHandPattern, $getVrHandReplacement, 1)
}

$twoHandCoreOld = 'if\(!virtualDetonator\)\s*OculusVR::ApplyTrackedWeaponTwoHandTransform\(hand,\s*weaponType,\s*&trackedWeapon\);'
$twoHandCoreNew = @'
if(!virtualDetonator && !VrBodyIK::WeaponOwnershipActive())
		OculusVR::ApplyTrackedWeaponTwoHandTransform(hand, weaponType,
			&trackedWeapon);
'@
if ($mainText -notmatch '!VrBodyIK::WeaponOwnershipActive\(\)') {
    $thMatches = [regex]::Matches($mainText, $twoHandCoreOld)
    if ($thMatches.Count -ne 1) {
        Write-Error "Body IK rigid weapon integration failed: core two-hand transform call was not found."
    }
    $mainText = [regex]::Replace(
        $mainText, $twoHandCoreOld, $twoHandCoreNew, 1)
}


# Body IK ON: apply the same +10 degree upward weapon tilt and +6 cm final
# Body IK ON: weapon orientation comes from the pitched IK wrist.
# Keep only the symmetric +6 cm forward shift in the core renderer.
$ikForwardOld = '(?ms)(trackedWeapon\s*=\s*trackedWeapon\s*\*\s*weaponRotationZAdjustment\s*;\s*)(// The same shortest-arc world rotation)'
if ($mainText -notmatch 'Body IK symmetric forward offset: 6 cm') {
    $fwMatches = [regex]::Matches($mainText, $ikForwardOld)
    if ($fwMatches.Count -ne 1) {
        Write-Error "Body IK weapon-forward integration failed: final trackedWeapon rotation was not found."
    }
    $ikForwardNew = @'
$1// Body IK symmetric forward offset: 6 cm. Orientation comes from
	// the pitched solved IK wrist; no model tilt or lateral shift here.
	if(VrBodyIK::WeaponOwnershipActive()){
		CVector ikBarrel = trackedWeapon.GetRight();
		if(ikBarrel.MagnitudeSqr() > 0.0001f){
			ikBarrel.Normalise();
			const float ikForwardOffset =
				(hand == 0) ? 0.000f : 0.060f;
			trackedWeapon.GetPosition() +=
				ikBarrel*ikForwardOffset;
		}

	}

	// Position-only baked LEFT-hand correction selected from the player's
	// final completed calibration. No runtime calibration mode remains.
	OculusVR::ApplyBakedLeftWeaponPositionToMatrix(hand, &trackedWeapon);
	$2
'@
    $mainText = [regex]::Replace(
        $mainText, $ikForwardOld, $ikForwardNew, 1)
}

[System.IO.File]::WriteAllText($mainPath, $mainText, [System.Text.UTF8Encoding]::new($false))
}

[System.IO.File]::WriteAllText($mainPath, $mainText, [System.Text.UTF8Encoding]::new($false))

$hasCoreHandOwnership =
    $mainText -match 'vrSkinnedHandsOwnPresentation' -and
    $mainText -match 'OculusVR::IsQuestFirstPersonOnFoot\(\)\s*&&\s*VrBodyIK::Enabled\(\)' -and
    $mainText -match 'VrBodyIK::VehicleEnabled\(\)' -and
    $mainText -match 'if\s*\(\s*!vrSkinnedHandsOwnPresentation\s*\)\s*\{\s*RenderVrTrackedHand\(0\);\s*RenderVrTrackedHand\(1\);'

if (-not $hasCoreHandOwnership) {
    Write-Error "Body IK hand ownership validation failed: core floating tracked hands are not correctly owned by the Body IK/Vehicle IK ownership policy."
}

Write-Host "[4/4] Copying librw with the Vulkan backend..."
robocopy (Join-Path $repo "librw") (Join-Path $Out "vendor\librw") /E /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) {
    Write-Error "Failed to copy librw (robocopy exit code $LASTEXITCODE)."
}

Write-Host ""
Write-Host "Done. Next steps:"
Write-Host "  cd $Out\android"
Write-Host "  gradle wrapper   (first time only, or open in Android Studio)"
Write-Host "  .\gradlew assembleDebug"
Write-Host "See BUILDING.md for prerequisites, game data, VR hands and headset installation."
