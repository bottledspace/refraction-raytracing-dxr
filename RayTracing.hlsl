RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0);

struct Payload {
	float4 color;
};

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
	float2 xy = index + 0.5f; // center in the middle of the pixel.
	float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;

	const float4x4 inv_proj = float4x4(
		10, 0, 0, 0,
		0, -10, 0, 0,
		0, 0, -0.831807, -0.166528,
		0, 0, -0.831807, 0.833472);
	
	// Unproject the pixel coordinate into a ray.
	float4 world = mul(float4(screenPos, 0, 1), inv_proj);

	world.xyz /= world.w;
	origin = float3(0.0,0.0,0.0);
	direction = normalize(world.xyz - origin);
}



[shader("raygeneration")]
void RayGen()
{
	/*float3 origin, dir;
	GenerateCameraRay(DispatchRaysIndex().xy, origin, dir);
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = dir;
	ray.TMin = 0.001;
	ray.TMax = 10000.0;*/
	
	RayDesc ray;
	ray.Origin = float3(0.0,0.0,0.0);
	ray.Direction = float3(
		cos(DispatchRaysIndex().x/640.0)*sin(DispatchRaysIndex().y/480.0),
		sin(DispatchRaysIndex().x/640.0)*sin(DispatchRaysIndex().y/480.0),
		cos(DispatchRaysIndex().y/480.0));
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
