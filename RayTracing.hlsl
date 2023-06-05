struct SceneConstants {
	float4x4 mvp;
};
struct Vertex {
	float3 position;
	float2 uv;
};

ConstantBuffer<SceneConstants> sceneConstants : register(b0);
RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<uint> Indices : register(t1, space0);
StructuredBuffer<Vertex> Vertices : register(t2, space0);

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
	float4 world = mul(float4(screenPos, 1, 0), sceneConstants.mvp);
	float4 origin = mul(float4(0, 0, 0, 1), sceneConstants.mvp);
	ray.Origin = origin.xyz;
	ray.Direction = normalize(world.xyz - origin.xyz);
}

[shader("raygeneration")]
void RayGen()
{
	RayDesc ray;
	GenerateCameraRay(DispatchRaysIndex().xy, ray);
	ray.TMin = 0.001;
	ray.TMax = 100.0;

	Payload payload;
	payload.color = float4(1.0, 1.0, 1.0, 1.0);
	
	TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);

	RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

/*[shader("anyhit")]
void AnyHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attrs)
{
	payload.color *= 0.5;

	//TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);

	IgnoreHit();
}*/

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attrs)
{
	float4 background = float4(1.0f, 0.0f, 0.4f, 1.0f);

	float3 inter = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
	payload.color = float4(inter.x,inter.y,inter.z,1.0);
	float3 A = Vertices[Indices[PrimitiveIndex() * 3 + 0]].position;
	float3 B = Vertices[Indices[PrimitiveIndex() * 3 + 1]].position;
	float3 C = Vertices[Indices[PrimitiveIndex() * 3 + 2]].position;
	payload.color.xyz = (A+B+C)/3.0f;
}

[shader("miss")]
void Miss(inout Payload payload)
{
}
