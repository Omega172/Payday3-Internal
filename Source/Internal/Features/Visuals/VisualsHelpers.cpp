#include "pch.h"
#include "VisualsHelpers.hpp"

#undef min
#undef max

namespace VisualsHelpers
{
	std::optional<ImVec4> CalculateScreenBoxFromTopBottom(SDK::APlayerController* pPlayerController, SDK::AActor* pActor,
		SDK::FVector vecTop, SDK::FVector vecBottom)
	{
		SDK::FVector2D vec2Top, vec2Bottom;
		if (!pPlayerController->ProjectWorldLocationToScreen(vecBottom, &vec2Bottom, false) ||
			!pPlayerController->ProjectWorldLocationToScreen(vecTop, &vec2Top, false))
			return {};

		float flHeight = std::abs(vec2Top.Y - vec2Bottom.Y);
		float flYawRad = pActor->K2_GetActorRotation().Yaw * (3.14159265358979323846f / 180.0f);
		float flWidthRatio = 0.25f + (std::abs(SDK::FVector(std::cos(flYawRad), std::sin(flYawRad), 0.f).GetNormalized().Dot(
			(pActor->K2_GetActorLocation() - pPlayerController->PlayerCameraManager->GetCameraLocation()).GetNormalized())) * 0.15f);

		float flWidth = flHeight * flWidthRatio;
		float flCenter = (vec2Bottom.X + vec2Top.X) / 2.0f;
		return ImVec4{flCenter - (flWidth / 2.0f), static_cast<float>(std::min(vec2Bottom.Y, vec2Top.Y)), flCenter + (flWidth / 2.0f), static_cast<float>(std::max(vec2Bottom.Y, vec2Top.Y))};
	}

	std::optional<ImVec4> CalculateScreenBoxForCharacter(SDK::USkeletalMeshComponent* pMeshComponent,
		SDK::APlayerController* pPlayerController, SDK::AActor* pActor)
	{
		if (!pMeshComponent || !pPlayerController || !pActor)
			return {};

		return CalculateScreenBoxFromTopBottom(
			pPlayerController,
			pActor,
			pMeshComponent->GetSocketLocation(SDK::UKismetStringLibrary::Conv_StringToName(L"HeadEnd")),
			pMeshComponent->GetSocketLocation(SDK::UKismetStringLibrary::Conv_StringToName(L"Reference"))
		);
	}

	void BuildBoneCache(SDK::USkeletalMeshComponent* mesh, Types::BoneCache& cache)
	{
		if (!mesh)
			return;

		const int numBones = mesh->GetNumBones();

		for (int i = 0; i < numBones; ++i)
		{
			const std::string bone = mesh->GetBoneName(i).ToString();

			if (bone == "Hips")
				cache.Hips = i;
			else if (bone == "Spine")
				cache.Spine = i;
			else if (bone == "Spine1")
				cache.Spine1 = i;
			else if (bone == "Spine2")
				cache.Spine2 = i;
			else if (bone == "Spine3")
				cache.Spine3 = i;
			else if (bone == "Neck")
				cache.Neck = i;
			else if (bone == "Head")
				cache.Head = i;
			else if (bone == "LeftShoulder")
				cache.LeftShoulder = i;
			else if (bone == "LeftArm")
				cache.LeftUpperArm = i;
			else if (bone == "LeftForeArm")
				cache.LeftForeArm = i;
			else if (bone == "LeftHand")
				cache.LeftHand = i;
			else if (bone == "RightShoulder")
				cache.RightShoulder = i;
			else if (bone == "RightArm")
				cache.RightUpperArm = i;
			else if (bone == "RightForeArm")
				cache.RightForeArm = i;
			else if (bone == "RightHand")
				cache.RightHand = i;
			else if (bone == "LeftUpLeg")
				cache.LeftUpperLeg = i;
			else if (bone == "LeftLeg")
				cache.LeftLowerLeg = i;
			else if (bone == "LeftFoot")
				cache.LeftFoot = i;
			else if (bone == "LeftToeBase")
				cache.LeftToe = i;
			else if (bone == "RightUpLeg")
				cache.RightUpperLeg = i;
			else if (bone == "RightLeg")
				cache.RightLowerLeg = i;
			else if (bone == "RightFoot")
				cache.RightFoot = i;
			else if (bone == "RightToeBase")
				cache.RightToe = i;
		}

		cache.Initialized = true;
	}

	void DrawBone(ImDrawList* pDrawList, SDK::APlayerController* pPlayerController, SDK::USkeletalMeshComponent* mesh,
		int parent, int child, ImU32 color)
	{
		if (parent < 0 || child < 0)
			return;

		auto parentWorld = mesh->GetSocketLocation(mesh->GetBoneName(parent));
		auto childWorld = mesh->GetSocketLocation(mesh->GetBoneName(child));
		SDK::FVector2D p0, p1;

		if (pPlayerController->ProjectWorldLocationToScreen(parentWorld, &p0, false) && pPlayerController->ProjectWorldLocationToScreen(childWorld, &p1, false))
		{
			pDrawList->AddLine(ImVec2(p0.X - 1, p0.Y - 1), ImVec2(p1.X - 1, p1.Y - 1), IM_COL32(0, 0, 0, 255), 1.1f);
			pDrawList->AddLine(ImVec2(p0.X, p0.Y), ImVec2(p1.X, p1.Y), color, 1.0f);
		}
	}
}
