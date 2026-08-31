#pragma once

#include <array>
#include <string_view>

namespace spice::sct::detail {

// SpiceSCT-owned concise names synchronized from the living GameCube handler
// catalog. They are descriptive metadata, not evidence that Dreamcast handlers
// have identical downstream semantics.
inline constexpr std::array<std::string_view, 266> kOpcodeCatalogNames{{
    "If / JumpIfFalse", "InvalidOpcode", "InvalidOpcode", "Switch", "InvalidOpcode",
    "SetByteVar", "SetIntVar", "SetFloatVar", "InvalidOpcode", "LabelOrStringPrefix",
    "Jump", "CallSubscript", "Return", "SkipRefreshPrefix / InvalidOpcode slot", "InvalidOpcode",
    "ForceNextFrame", "Sleep", "SetBit", "UnsetBit", "InvertBit",
    "GiveItem", "TakeItem", "ReturnOneNoAdvance", "LoadMldFile", "DisplayMessageByFooterOffset",
    "SelectMenuPrompt", "Category6EnterRuntime", "LetsTalk", "DefMO", "PlayMO",
    "SetExpression", "PutA", "PutP", "MVCB2_CameraVectorPath", "MVR",
    "MV", "RollA", "RollP", "NecM", "NecA",
    "SetCameraDescMode2SlotScalar", "Ret", "Cl", "LoadScriptByName", "ReturnOneNoAdvance",
    "RestoreSavedCameraPose", "InterpolateCameraEulerRotation", "InterpolateCameraLookAtRotation", "SetStageWorksheetStateBucket", "SetCameraLensScalar",
    "SetCameraPose", "PlayIndexedSoundCue", "SetCameraPositionLookAt", "Mvj_JumpMove", "LoadSoundBankPairNoop",
    "SetAudioTrackCuePair", "SetCurrentTrackLevelEnvelope", "InvalidInsBg", "InvalidDellBg", "ClearTransitionOverlayTimed",
    "ApplyTransitionOverlayTimed", "Hopen", "Hclose", "Hchange", "Hread",
    "Hset", "PlayIndexedSoundCueRawClass", "InvalidKill", "RefreshStageWorksheetStateBucketCache", "LoadSoundMlt",
    "CameraDescMode1SetVectorMask08", "CameraDescMode1SetVector10", "CameraDescDeferredMode1Work", "CameraDescMode1MultiSlot", "CameraDescMode5TwoVec",
    "CameraDescMode6Full", "CameraDescMode15Wait", "SetPendingObjectTransformMode1", "PlayColMO", "SetFieldFogColorAndRangeBlendDefaultA",
    "DisableFieldOverlayColorBlend", "SetFldSkyMode5LightRecord", "SetFldSkyMode0Color", "SetFldSkyMode1LightRecord", "SetFldSkyIndexedLightRecord",
    "FloatingFixtureStateCommand0_UkisuguR", "FloatingFixtureStateCommand1_Ukisugu1", "FloatingFixtureStateCommand2_Ukinoru", "FloatingFixtureStateCommand3_Ukitome", "MVF_PathMove",
    "PathMoveVariant16_CubicPrecompute", "SetFieldOverlayEventCollisionTargetPair", "SetFieldOverlayAutoTargetFromStageEvent", "SetFieldOverlayTargetLinkEnabled", "SetFieldOverlayTargetId",
    "CameraDescMode1HeightVariantA", "CameraDescMode1HeightVariantB", "ChangeParts", "PlayEffectWait", "StopEffectWait / TaskOffEffectWait",
    "TextChange", "AudioControlByDecodedIdFiltered", "AudioControlByDecodedIdUnfiltered", "ClearIndexedSoundCueSlots", "StartFieldEffectInterpolated",
    "UpdateFieldEffectInterpolated", "ParentOn", "ParentOff", "PathMoveFinalFacing", "SetFieldFogColorAndRangeBlend",
    "ReleaseOrCancelResourceByOffset", "CameraDescMode7TimedPair", "TriggerBattle", "WaitResourceByOffset", "LinkOrClearEventTableEntryByTblId",
    "LoadMe002zGameState15", "CreateSimpleIndexedAudioCueTask", "CWait", "FadeIn_Fin", "FadeOut_ClF",
    "PlayMO3", "MVCA_CameraAnglePath", "CameraDescMode8FieldControl", "TrackUp", "SetCameraLensScalarTimed",
    "ClearActiveCameraAndFffeMetadataTasks", "ReleaseMetadata5TaskTargets", "ReleaseMetadata2TaskTargetsAndResetChildField", "PlayMO2", "ScheduleInst",
    "MV2", "SetFldSkyMode0ColorKeyframes", "SetFldSkyMode1LightKeyframes", "SetFldSkyIndexedLightKeyframes", "Proll",
    "SetMldWorksheetFieldFogBpStateGateForList", "Scale", "SetPendingPlayerObjectSwapId", "DisplaySaveMenu", "MVCALP_GlobalLoopCameraPath",
    "FadeOutGrayOverlay", "FadeInGrayOverlay", "FadeOutCustomOverlay", "ReturnZeroNoAdvance", "DisplayMessage",
    "SetRandomIntVar16PlusSlot0To9999", "SnapshotObjectTransform", "SpawnDistanceAngleCallbackTask", "SetFldSkyMode5LightKeyframes", "EnterRelatedObjectRuntimeTasks",
    "RefreshRelatedObjectTasks", "SetObjectMotionPreviousFrameRaw", "ResetObjectAndLinkedTransformDefaults", "SetControllerRumblePattern", "OpenTreasure",
    "SelectChoice", "RestoreStageWorksheetTransform", "AddPartyMember", "RemovePartyMember", "MVCA3_EasedCameraPath",
    "SetPendingObjectTransformMode2", "MV3", "CameraDescMode9Toggle", "CameraDescMode10Full", "HCLR",
    "BuildShipBattleScenePhaseEntry", "BindShipBattlePath", "InitShipBattleRuntime", "SetEffectIntensityAutoMode", "SetEffectModeFloatQuad",
    "EnableShipBattleSceneTimerStep", "PauseShipBattleSceneTimerStep", "BeginShipBattleSceneTable", "EndShipBattleSceneTable", "SetCurrentShipBattleScene",
    "AttachedEffectCommand", "PartsScale", "RunDiscoveryFoundSequence", "CameraDescMode2LookAt", "CameraDescMode1Vector10",
    "HSetTask", "LoadShop", "InvalidOpcode", "CheckItemAmount", "CameraDescMode13",
    "SetShipBattleControlPairShownState", "SetShipBattleControlPairHiddenState", "SetShipBattleControlB8ShownState", "SetShipBattleControlB8HiddenState", "InvalidOpcode",
    "MapDefMO", "SnapshotShipBattleShipInfo", "ShipInfoNoOpStubA", "ShipInfoNoOpStubB", "ShipInfoNoOpStubC",
    "KeyWait", "RequestFieldInteractionCameraState", "SetShipBattleCameraPathObjects", "SetShadow", "ReSetShadow",
    "InvalidOpcode", "RestoreHealthAll", "RestoreMagicAll", "RemoveDistanceAngleCallbackTaskById", "Stub_204",
    "Stub_205", "SnapshotShipBattleTimer", "SetSoundHandleLevelEnvelope", "EnableShipBattleCameraMotion", "DisableShipBattleCameraMotion",
    "WarpCurrentAreaByString", "ReturnToSavedWarpArea", "SetShipBattleEnemyCommand", "SetShipBattleCommandBytePairs", "WarpByStringPrevArea40000",
    "PlaySoundByMode", "FinalizeQueuedAudioLoad", "FieldSonarBattleMenuWorkCommand", "MarkCrewMemberAvailabilityAndRefreshShipStats", "ConvertSkyMapMode",
    "StartObjectTargetedTransformState", "StopObjectSpecialState", "SelectShipAndRebuildCrewShipStats", "SetShipBattleCameraSelectEntry", "SetShipBattleCameraKeyEntry",
    "SetShipBattleCameraObjectMap", "SetShipBattleTurnCameraRange", "RemovePartyMemberFullEquipment", "SwapPlayerGroup", "UnifyPlayerGroups",
    "CameraDescMode11FieldControl", "StartDiscoveryGuildMenuTask", "SetShipBattleCameraCompositeEntry", "PickupChamTypeItem", "AdvanceOnlyStub234",
    "SetKMapSelectedObject", "SetKMapProjectedUniformRectPackedColorControl", "SetGameState0f", "ReturnToOverworldAtPosition", "StartRumbleFadeOut",
    "StartFlightVisualController", "StopFlightVisualController", "SetAbilityFlagMirrorBit", "StartSetPieceDistanceAngleTask", "GiveDirectOneUnitReward",
    "PlayEffectContinue", "StopEffectContinue / TaskOffEffectContinue", "ResetAudioStreamingState", "QueueSoundMltLoad", "NormalFace",
    "WaitSoundMltLoad", "SetFldSkyMode1LightRandomScale", "SetFldSkyMode0ColorRandomScale", "SetFldSkyIndexedLightRandomScale", "StartShipBattleTurnCameraTask",
    "StopShipBattleTurnCameraTask", "UnusedConsumeOneParam", "ExitShipBattleToScript", "InvokeTargetObjectCallbacks", "HamachouHermitStatDialog",
    "RestoreShipHealthAll", "DomingoDiscoveryDialog", "FadeInCustomOverlay", "ShipBattleCameraAuxDispatch", "ImmediateTransformSet",
    "GeneratedReputationListDialog",
}};

} // namespace spice::sct::detail
