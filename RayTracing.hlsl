struct SceneConstants {
	float4x4 mvp;
};

ConstantBuffer<SceneConstants> sceneConstants : register(b0);
RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0);

struct Payload {
	float4 color;
};

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out RayDesc ray)
{
	float2 xy = index + 0.5f; // center in the middle of the pixel.
	float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;
	
	// Unproject the pixel coordinate into a ray.
	float4 world = mul(float4(screenPos, 1, 1), sceneConstants.mvp);
	float4 origin = mul(float4(0, 0, 0, 1), sceneConstants.mvp);
	world.xyz /= world.w;
	ray.Origin = world.xyz;
	ray.Direction = normalize(world.xyz - origin.xyz/origin.w);
}

[shader("raygeneration")]
void RayGen()
{
	RayDesc ray;
	GenerateCameraRay(DispatchRaysIndex().xy, ray);
	ray.TMin = 0.001;
	ray.TMax = 10000.0;

	Payload payload;
	payload.color = float4(1.0, 1.0, 0.0, 1.0);
	
	TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);

	RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attrs)
{
	float4 background = float4(1.0f, 0.0f, 0.4f, 1.0f);
	payload.color = background;
}

[shader("miss")]
void Miss(inout Payload payload)
{
	float4 background = float4(0.2f, 0.3f, 0.8f, 1.0f);
	payload.color = background;
}
