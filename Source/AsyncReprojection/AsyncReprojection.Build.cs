using UnrealBuildTool;
using System.IO;

public class AsyncReprojection : ModuleRules
{
	public AsyncReprojection(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
			);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"DeveloperSettings",
				"Projects",
				"RenderCore",
				"Renderer",
				"RHI",
				"Slate",
				"SlateCore",
			}
			);

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"),
				Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"),
			}
			);

		PrivateDefinitions.Add("ASYNC_REPROJECTION_WITH_XR=0");
	}
}
